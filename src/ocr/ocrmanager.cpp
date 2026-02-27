#include "ocrmanager.h"
#include "ocr/debug/ocrdebugdumper.h"
#include "ocr/postprocess/platepostprocessor.h"
#include "ocr/preprocess/platepreprocessor.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr int kDefaultInputWidth = 320;
constexpr int kDefaultInputHeight = 48;
constexpr int kMinWarpWidth = 96;
constexpr double kMinContourArea = 500.0;
constexpr double kMinQuadArea = 400.0;
constexpr double kMaxTopEdgeAngleDeg = 45.0;
constexpr double kMinBinarySharpness = 55.0;
constexpr double kLowQualitySharpness = 80.0;
constexpr double kPreferredBinarySharpness = 95.0;

cv::Mat toRgbImage(const cv::Mat &src)
{
  if (src.empty())
  {
    return cv::Mat();
  }

  if (src.type() == CV_8UC3)
  {
    return src.clone();
  }

  if (src.type() == CV_8UC1)
  {
    cv::Mat rgb;
    cv::cvtColor(src, rgb, cv::COLOR_GRAY2RGB);
    return rgb;
  }

  if (src.type() == CV_8UC4)
  {
    cv::Mat rgb;
    cv::cvtColor(src, rgb, cv::COLOR_RGBA2RGB);
    return rgb;
  }

  return cv::Mat();
}

cv::Mat toGrayImage(const cv::Mat &src)
{
  if (src.empty())
  {
    return cv::Mat();
  }

  if (src.type() == CV_8UC1)
  {
    return src.clone();
  }

  if (src.type() == CV_8UC3)
  {
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
    return gray;
  }

  if (src.type() == CV_8UC4)
  {
    cv::Mat gray;
    cv::cvtColor(src, gray, cv::COLOR_RGBA2GRAY);
    return gray;
  }

  return cv::Mat();
}

cv::Mat grayToRgb(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  cv::Mat rgb;
  cv::cvtColor(gray, rgb, cv::COLOR_GRAY2RGB);
  return rgb;
}

cv::Mat applyClahe(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.8, cv::Size(8, 8));
  cv::Mat out;
  clahe->apply(gray, out);
  return out;
}

cv::Mat denoiseGray(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  cv::Mat out;
  cv::bilateralFilter(gray, out, 5, 35.0, 35.0);
  return out;
}

cv::Mat autoGammaCorrect(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  const double meanValue = cv::mean(gray)[0];
  if (meanValue <= 1.0 || meanValue >= 254.0)
  {
    return gray.clone();
  }

  const double normalizedMean = meanValue / 255.0;
  const double targetMean = 0.58;
  const double gamma = std::clamp(
      std::log(targetMean) / std::log(normalizedMean), 0.75, 1.35);

  cv::Mat lut(1, 256, CV_8U);
  for (int i = 0; i < 256; ++i)
  {
    const double normalized = static_cast<double>(i) / 255.0;
    lut.at<uchar>(i) = static_cast<uchar>(std::clamp(
        std::lround(std::pow(normalized, gamma) * 255.0), 0L, 255L));
  }

  cv::Mat corrected;
  cv::LUT(gray, lut, corrected);
  return corrected;
}

cv::Mat unsharpMask(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  cv::Mat blurred;
  cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 1.1);
  cv::Mat sharpened;
  cv::addWeighted(gray, 1.45, blurred, -0.45, 0.0, sharpened);
  return sharpened;
}

cv::Mat contrastStretch(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  cv::Mat out;
  cv::normalize(gray, out, 0, 255, cv::NORM_MINMAX);
  return out;
}

double estimateSharpness(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return 0.0;
  }

  cv::Mat laplacian;
  cv::Laplacian(gray, laplacian, CV_64F);
  cv::Scalar mean;
  cv::Scalar stddev;
  cv::meanStdDev(laplacian, mean, stddev);
  return stddev[0] * stddev[0];
}

double estimateContrast(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return 0.0;
  }

  cv::Scalar mean;
  cv::Scalar stddev;
  cv::meanStdDev(gray, mean, stddev);
  return stddev[0];
}

cv::Mat upscaleForOcr(const cv::Mat &rgb, double *scaleOut,
                      double *sharpnessOut = nullptr)
{
  if (scaleOut)
  {
    *scaleOut = 1.0;
  }
  if (sharpnessOut)
  {
    *sharpnessOut = 0.0;
  }

  const cv::Mat gray = toGrayImage(rgb);
  if (gray.empty())
  {
    return rgb.clone();
  }

  const double sharpness = estimateSharpness(gray);
  if (sharpnessOut)
  {
    *sharpnessOut = sharpness;
  }

  double scale = 1.0;
  if (rgb.cols < 150 || rgb.rows < 38)
  {
    scale = 2.2;
  }
  else if (rgb.cols < 190 || rgb.rows < 44)
  {
    scale = 1.75;
  }
  else if (rgb.cols < 230 || sharpness < 110.0)
  {
    scale = 1.4;
  }

  if (sharpness < 45.0)
  {
    scale = std::max(scale, 2.3);
  }
  else if (sharpness < 80.0)
  {
    scale = std::max(scale, 1.9);
  }

  scale = std::clamp(scale, 1.0, 2.5);
  if (scale <= 1.05)
  {
    return rgb.clone();
  }

  cv::Mat upscaled;
  cv::resize(rgb, upscaled, cv::Size(), scale, scale, cv::INTER_CUBIC);
  if (scaleOut)
  {
    *scaleOut = scale;
  }
  return upscaled;
}

cv::Mat makeBinaryOcrInput(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  const double sharpness = estimateSharpness(gray);
  const double contrast = estimateContrast(gray);
  if (sharpness < kMinBinarySharpness || contrast < 32.0)
  {
    return gray.clone();
  }

  cv::Mat filtered;
  cv::medianBlur(gray, filtered, 3);

  cv::Mat binary;
  cv::threshold(filtered, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  const cv::Mat kernel =
      cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
  cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
  cv::morphologyEx(binary, binary, cv::MORPH_CLOSE,
                   kernel);

  const double whiteRatio =
      static_cast<double>(cv::countNonZero(binary)) / static_cast<double>(binary.total());
  if (whiteRatio < 0.55 || whiteRatio > 0.96)
  {
    return gray.clone();
  }

  const double blackRatio = 1.0 - whiteRatio;
  if (blackRatio < 0.04 || blackRatio > 0.42)
  {
    return gray.clone();
  }

  return binary;
}

cv::Mat suppressEdgeNoise(const cv::Mat &gray)
{
  if (gray.empty() || gray.type() != CV_8UC1)
  {
    return gray.clone();
  }

  cv::Mat working = gray.clone();
  const bool isBinary = (cv::countNonZero((gray != 0) & (gray != 255)) == 0);
  if (!isBinary)
  {
    return working;
  }

  cv::Mat inverted;
  cv::bitwise_not(working, inverted);

  cv::Mat labels;
  cv::Mat stats;
  cv::Mat centroids;
  const int componentCount = cv::connectedComponentsWithStats(
      inverted, labels, stats, centroids, 8, CV_32S);
  if (componentCount <= 1)
  {
    return working;
  }

  const int edgeMarginX = std::max(3, working.cols / 32);
  const int edgeMarginY = std::max(2, working.rows / 10);
  const int imageArea = working.cols * working.rows;

  for (int label = 1; label < componentCount; ++label)
  {
    const int x = stats.at<int>(label, cv::CC_STAT_LEFT);
    const int y = stats.at<int>(label, cv::CC_STAT_TOP);
    const int w = stats.at<int>(label, cv::CC_STAT_WIDTH);
    const int h = stats.at<int>(label, cv::CC_STAT_HEIGHT);
    const int area = stats.at<int>(label, cv::CC_STAT_AREA);

    const bool touchesLeft = x <= edgeMarginX;
    const bool touchesRight = (x + w) >= (working.cols - edgeMarginX);
    const bool touchesTop = y <= edgeMarginY;
    const bool touchesBottom = (y + h) >= (working.rows - edgeMarginY);
    const bool edgeTouch = touchesLeft || touchesRight || touchesTop || touchesBottom;

    const bool tinyBlob = area <= std::max(10, imageArea / 90);
    const bool narrowBlob = w <= std::max(3, working.cols / 18);
    const bool shortBlob = h <= std::max(4, working.rows / 3);

    if (!edgeTouch || !tinyBlob || (!narrowBlob && !shortBlob))
    {
      continue;
    }

    for (int row = 0; row < labels.rows; ++row)
    {
      int *labelPtr = labels.ptr<int>(row);
      uchar *pixelPtr = working.ptr<uchar>(row);
      for (int col = 0; col < labels.cols; ++col)
      {
        if (labelPtr[col] == label)
        {
          pixelPtr[col] = 255;
        }
      }
    }
  }

  return working;
}

bool shouldInvertForOcr(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return false;
  }

  const cv::Mat kernel =
      cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
  cv::Mat topHat;
  cv::Mat blackHat;
  cv::morphologyEx(gray, topHat, cv::MORPH_TOPHAT, kernel);
  cv::morphologyEx(gray, blackHat, cv::MORPH_BLACKHAT, kernel);

  const double brightStrokeEnergy = cv::mean(topHat)[0];
  const double darkStrokeEnergy = cv::mean(blackHat)[0];
  return brightStrokeEnergy > (darkStrokeEnergy * 1.15);
}

cv::Rect clampRectToImage(const cv::Rect &rect, const cv::Size &size)
{
  const int x = std::clamp(rect.x, 0, size.width);
  const int y = std::clamp(rect.y, 0, size.height);
  const int right = std::clamp(rect.x + rect.width, 0, size.width);
  const int bottom = std::clamp(rect.y + rect.height, 0, size.height);
  return cv::Rect(x, y, std::max(0, right - x), std::max(0, bottom - y));
}

cv::Rect unionRects(const cv::Rect &lhs, const cv::Rect &rhs)
{
  if (lhs.empty())
  {
    return rhs;
  }
  if (rhs.empty())
  {
    return lhs;
  }
  return lhs | rhs;
}

std::vector<int> smoothProjection(const std::vector<int> &values, const int radius)
{
  if (values.empty() || radius <= 0)
  {
    return values;
  }

  std::vector<int> out(values.size(), 0);
  for (int i = 0; i < static_cast<int>(values.size()); ++i)
  {
    int sum = 0;
    int count = 0;
    for (int j = std::max(0, i - radius);
         j <= std::min(static_cast<int>(values.size()) - 1, i + radius); ++j)
    {
      sum += values[j];
      ++count;
    }
    out[i] = (count > 0) ? (sum / count) : values[i];
  }
  return out;
}

bool findProjectionRange(const std::vector<int> &values, const int threshold,
                         const int startIndex, const int endIndex,
                         int *firstOut, int *lastOut)
{
  if (!firstOut || !lastOut || values.empty() || startIndex > endIndex)
  {
    return false;
  }

  int first = -1;
  int last = -1;
  for (int i = startIndex; i <= endIndex; ++i)
  {
    if (values[i] >= threshold)
    {
      first = i;
      break;
    }
  }
  for (int i = endIndex; i >= startIndex; --i)
  {
    if (values[i] >= threshold)
    {
      last = i;
      break;
    }
  }

  if (first < 0 || last < first)
  {
    return false;
  }

  *firstOut = first;
  *lastOut = last;
  return true;
}

cv::Rect fallbackInnerTrimRect(const cv::Mat &gray);

cv::Rect estimatePlateContentRect(const cv::Mat &gray)
{
  if (gray.empty() || gray.cols < 24 || gray.rows < 16)
  {
    return cv::Rect();
  }

  const double sharpness = estimateSharpness(gray);
  const bool lowQuality = (sharpness < kLowQualitySharpness);

  const cv::Mat denoised = denoiseGray(gray);
  const cv::Mat clahe = applyClahe(denoised.empty() ? gray : denoised);
  const cv::Mat stretched = contrastStretch(clahe.empty() ? gray : clahe);
  const cv::Mat base = stretched.empty() ? (clahe.empty() ? gray : clahe) : stretched;

  const cv::Mat kernel =
      cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
  cv::Mat topHat;
  cv::Mat blackHat;
  cv::morphologyEx(base, topHat, cv::MORPH_TOPHAT, kernel);
  cv::morphologyEx(base, blackHat, cv::MORPH_BLACKHAT, kernel);

  cv::Mat response;
  cv::max(topHat, blackHat, response);
  cv::GaussianBlur(response, response, cv::Size(3, 3), 0.0);

  cv::Mat mask;
  cv::threshold(response, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  const int borderX = std::max(2, gray.cols / (lowQuality ? 24 : 48));
  const int borderY = std::max(2, gray.rows / (lowQuality ? 10 : 24));
  mask.colRange(0, borderX).setTo(0);
  mask.colRange(gray.cols - borderX, gray.cols).setTo(0);
  mask.rowRange(0, borderY).setTo(0);
  mask.rowRange(gray.rows - borderY, gray.rows).setTo(0);

  cv::morphologyEx(mask, mask, cv::MORPH_CLOSE,
                   cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));

  std::vector<int> colCounts(gray.cols, 0);
  std::vector<int> rowCounts(gray.rows, 0);
  for (int x = 0; x < gray.cols; ++x)
  {
    colCounts[x] = cv::countNonZero(mask.col(x));
  }
  for (int y = 0; y < gray.rows; ++y)
  {
    rowCounts[y] = cv::countNonZero(mask.row(y));
  }

  const std::vector<int> smoothCols = smoothProjection(colCounts, 2);
  const std::vector<int> smoothRows = smoothProjection(rowCounts, 1);

  const int colThreshold =
      std::max(2, static_cast<int>(std::lround(gray.rows * (lowQuality ? 0.16 : 0.12))));
  const int rowThreshold =
      std::max(2, static_cast<int>(std::lround(gray.cols * (lowQuality ? 0.11 : 0.08))));

  int left = -1;
  int right = -1;
  int top = -1;
  int bottom = -1;
  const bool hasCols = findProjectionRange(
      smoothCols, colThreshold, borderX, gray.cols - borderX - 1, &left, &right);
  const bool hasRows = findProjectionRange(
      smoothRows, rowThreshold, borderY, gray.rows - borderY - 1, &top, &bottom);

  if (hasCols && hasRows)
  {
    const cv::Rect projectedRect(left, top, (right - left) + 1, (bottom - top) + 1);
    if (projectedRect.width >= static_cast<int>(std::lround(gray.cols * 0.45)) &&
        projectedRect.height >= static_cast<int>(std::lround(gray.rows * 0.28)))
    {
      const int padX =
          std::max(3, static_cast<int>(std::lround(projectedRect.width *
                                                   (lowQuality ? 0.09 : 0.08))));
      const int padY =
          std::max(3, static_cast<int>(std::lround(projectedRect.height *
                                                   (lowQuality ? 0.15 : 0.15))));
      const cv::Rect expanded = clampRectToImage(
          cv::Rect(projectedRect.x - padX, projectedRect.y - padY,
                   projectedRect.width + (padX * 2),
                   projectedRect.height + (padY * 2)),
          gray.size());
      if (expanded.width >= static_cast<int>(std::lround(gray.cols *
                                                         (lowQuality ? 0.72 : 0.60))) &&
          expanded.height >= static_cast<int>(std::lround(gray.rows *
                                                          (lowQuality ? 0.70 : 0.55))) &&
          (expanded.width < (gray.cols - 2) || expanded.height < (gray.rows - 2)))
      {
        return expanded;
      }

      const cv::Rect tightExpanded = clampRectToImage(
          cv::Rect(projectedRect.x - std::max(2, padX / 2),
                   projectedRect.y - std::max(2, padY / 2),
                   projectedRect.width + std::max(4, padX),
                   projectedRect.height + std::max(4, padY)),
          gray.size());
      if (!tightExpanded.empty() &&
          (tightExpanded.width < (gray.cols - 2) || tightExpanded.height < (gray.rows - 2)))
      {
        return tightExpanded;
      }
    }
  }

  cv::Mat labels;
  cv::Mat stats;
  cv::Mat centroids;
  const int componentCount = cv::connectedComponentsWithStats(
      mask, labels, stats, centroids, 8, CV_32S);
  if (componentCount <= 1)
  {
    return cv::Rect();
  }

  const int imageArea = gray.cols * gray.rows;
  cv::Rect contentRect;
  int keptComponents = 0;
  for (int label = 1; label < componentCount; ++label)
  {
    const int x = stats.at<int>(label, cv::CC_STAT_LEFT);
    const int y = stats.at<int>(label, cv::CC_STAT_TOP);
    const int w = stats.at<int>(label, cv::CC_STAT_WIDTH);
    const int h = stats.at<int>(label, cv::CC_STAT_HEIGHT);
    const int area = stats.at<int>(label, cv::CC_STAT_AREA);
    const bool touchesBorder =
        (x <= borderX || y <= borderY ||
         (x + w) >= (gray.cols - borderX) ||
         (y + h) >= (gray.rows - borderY));

    if (area < std::max(8, imageArea / 700))
    {
      continue;
    }
    if (area > (imageArea / 4))
    {
      continue;
    }
    if (h < std::max(8, gray.rows / 3) || h > (gray.rows - borderY))
    {
      continue;
    }
    if (w > std::max(10, gray.cols / 3))
    {
      continue;
    }
    if (touchesBorder && area > (imageArea / 25))
    {
      continue;
    }

    contentRect = unionRects(contentRect, cv::Rect(x, y, w, h));
    ++keptComponents;
  }

  if (keptComponents < 2 || contentRect.empty())
  {
    return cv::Rect();
  }

  const int padX = std::max(4, static_cast<int>(std::lround(contentRect.width *
                                                            (lowQuality ? 0.16 : 0.10))));
  const int padY = std::max(3, static_cast<int>(std::lround(contentRect.height *
                                                            (lowQuality ? 0.24 : 0.18))));
  const cv::Rect expanded = clampRectToImage(
      cv::Rect(contentRect.x - padX, contentRect.y - padY,
               contentRect.width + (padX * 2), contentRect.height + (padY * 2)),
      gray.size());

  if (expanded.width < static_cast<int>(std::lround(gray.cols *
                                                    (lowQuality ? 0.72 : 0.50))) ||
      expanded.height < static_cast<int>(std::lround(gray.rows *
                                                     (lowQuality ? 0.68 : 0.50))))
  {
    return cv::Rect();
  }

  if (expanded.width >= (gray.cols - 2) && expanded.height >= (gray.rows - 2))
  {
    return fallbackInnerTrimRect(gray);
  }

  return expanded;
}

cv::Mat trimPlateToContent(const cv::Mat &rgb, cv::Rect *cropRectOut)
{
  if (cropRectOut)
  {
    *cropRectOut = cv::Rect();
  }

  const cv::Mat gray = toGrayImage(rgb);
  if (gray.empty())
  {
    return cv::Mat();
  }

  const cv::Rect cropRect = estimatePlateContentRect(gray);
  if (cropRect.empty())
  {
    const cv::Rect fallbackRect = fallbackInnerTrimRect(gray);
    if (fallbackRect.empty())
    {
      return rgb.clone();
    }

    if (cropRectOut)
    {
      *cropRectOut = fallbackRect;
    }
    return rgb(fallbackRect).clone();
  }

  if (cropRectOut)
  {
    *cropRectOut = cropRect;
  }
  return rgb(cropRect).clone();
}

cv::Rect fallbackInnerTrimRect(const cv::Mat &gray)
{
  if (gray.empty() || gray.cols < 80 || gray.rows < 24)
  {
    return cv::Rect();
  }

  const int padX = std::max(2, static_cast<int>(std::lround(gray.cols * 0.035)));
  const int padTop = std::max(2, static_cast<int>(std::lround(gray.rows * 0.10)));
  const int padBottom = std::max(2, static_cast<int>(std::lround(gray.rows * 0.08)));
  return clampRectToImage(
      cv::Rect(padX, padTop, std::max(1, gray.cols - (padX * 2)),
               std::max(1, gray.rows - padTop - padBottom)),
      gray.size());
}

void appendVariant(std::vector<ocr::preprocess::OcrInputVariant> *variants,
                   const QString &tag, const cv::Mat &imageRgb)
{
  if (!variants || imageRgb.empty())
  {
    return;
  }

  cv::Mat rgb = toRgbImage(imageRgb);
  if (rgb.empty())
  {
    return;
  }

  variants->push_back(ocr::preprocess::OcrInputVariant{tag, rgb});
}

bool buildOcrVariants(
    const cv::Mat &normalizedRgb, cv::Mat *enhancedGrayOut,
    std::vector<ocr::preprocess::OcrInputVariant> *variantsOut)
{
  if (!enhancedGrayOut || !variantsOut)
  {
    return false;
  }

  *enhancedGrayOut = cv::Mat();
  variantsOut->clear();

  const cv::Mat rgb = toRgbImage(normalizedRgb);
  if (rgb.empty())
  {
    return false;
  }

  const cv::Mat gray = toGrayImage(rgb);
  if (gray.empty())
  {
    return false;
  }

  const cv::Mat denoisedGray = denoiseGray(gray);
  const cv::Mat claheGray = applyClahe(denoisedGray.empty() ? gray : denoisedGray);
  const cv::Mat stretchedGray =
      contrastStretch(claheGray.empty() ? gray : claheGray);
  const cv::Mat gammaGray =
      autoGammaCorrect(stretchedGray.empty() ? (claheGray.empty() ? gray : claheGray)
                                             : stretchedGray);
  const cv::Mat sharpGray =
      unsharpMask(gammaGray.empty() ? (stretchedGray.empty()
                                           ? (claheGray.empty() ? gray : claheGray)
                                           : stretchedGray)
                                    : gammaGray);
  const double sharpness = estimateSharpness(sharpGray.empty() ? gray : sharpGray);
  const double contrast = estimateContrast(sharpGray.empty() ? gray : sharpGray);

  cv::Mat singleInputGray =
      sharpGray.empty() ? (gammaGray.empty() ? gray : gammaGray) : sharpGray;
  if (singleInputGray.empty())
  {
    return false;
  }

  if (shouldInvertForOcr(singleInputGray))
  {
    cv::bitwise_not(singleInputGray, singleInputGray);
  }

  const cv::Mat binaryGray = makeBinaryOcrInput(singleInputGray);
  const double finalSharpness = estimateSharpness(singleInputGray);
  const double finalContrast = estimateContrast(singleInputGray);
  const bool preferBinary =
      finalSharpness >= kPreferredBinarySharpness &&
      finalContrast >= std::max(40.0, contrast * 0.90);
  if (!binaryGray.empty() && preferBinary)
  {
    singleInputGray = suppressEdgeNoise(binaryGray);
  }
  else if (sharpness < kLowQualitySharpness || finalSharpness < kPreferredBinarySharpness)
  {
    // Small or blurry plates survive better with softer grayscale input.
    cv::Mat softened;
    cv::GaussianBlur(singleInputGray, softened, cv::Size(3, 3), 0.0);
    singleInputGray = softened;
  }

  *enhancedGrayOut = singleInputGray.clone();

  appendVariant(variantsOut, QStringLiteral("gray_single"),
                grayToRgb(singleInputGray));

  return !variantsOut->empty();
}

bool detectPlateContour(const cv::Mat &gray, std::vector<cv::Point> *contourOut,
                        int *contourCountOut, double *areaOut)
{
  if (contourOut)
  {
    contourOut->clear();
  }
  if (contourCountOut)
  {
    *contourCountOut = 0;
  }
  if (areaOut)
  {
    *areaOut = 0.0;
  }
  if (gray.empty() || !contourOut)
  {
    return false;
  }

  cv::Mat blurred;
  cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);

  cv::Mat edges;
  cv::Canny(blurred, edges, 50.0, 150.0);
  cv::dilate(edges, edges, cv::Mat(), cv::Point(-1, -1), 1);

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(edges, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
  if (contourCountOut)
  {
    *contourCountOut = static_cast<int>(contours.size());
  }

  double bestArea = 0.0;
  for (const std::vector<cv::Point> &contour : contours)
  {
    const double area = std::abs(cv::contourArea(contour));
    if (area < kMinContourArea || area <= bestArea)
    {
      continue;
    }

    bestArea = area;
    *contourOut = contour;
  }

  if (areaOut)
  {
    *areaOut = bestArea;
  }
  return !contourOut->empty();
}

std::array<cv::Point2f, 4> orderQuadPoints(const std::vector<cv::Point2f> &points)
{
  std::array<cv::Point2f, 4> ordered{};
  if (points.size() != 4)
  {
    return ordered;
  }

  auto sumOf = [](const cv::Point2f &pt) { return pt.x + pt.y; };
  auto diffOf = [](const cv::Point2f &pt) { return pt.x - pt.y; };

  ordered[0] = *std::min_element(points.begin(), points.end(),
                                 [&](const cv::Point2f &lhs,
                                     const cv::Point2f &rhs) {
                                   return sumOf(lhs) < sumOf(rhs);
                                 });
  ordered[2] = *std::max_element(points.begin(), points.end(),
                                 [&](const cv::Point2f &lhs,
                                     const cv::Point2f &rhs) {
                                   return sumOf(lhs) < sumOf(rhs);
                                 });
  ordered[1] = *std::max_element(points.begin(), points.end(),
                                 [&](const cv::Point2f &lhs,
                                     const cv::Point2f &rhs) {
                                   return diffOf(lhs) < diffOf(rhs);
                                 });
  ordered[3] = *std::min_element(points.begin(), points.end(),
                                 [&](const cv::Point2f &lhs,
                                     const cv::Point2f &rhs) {
                                   return diffOf(lhs) < diffOf(rhs);
                                 });
  return ordered;
}

bool hasDistinctQuadPoints(const std::array<cv::Point2f, 4> &quad)
{
  for (size_t i = 0; i < quad.size(); ++i)
  {
    for (size_t j = i + 1; j < quad.size(); ++j)
    {
      if (cv::norm(quad[i] - quad[j]) < 2.0)
      {
        return false;
      }
    }
  }
  return true;
}

bool extractQuadPoints(const std::vector<cv::Point> &contour,
                       std::array<cv::Point2f, 4> *quadOut)
{
  if (!quadOut || contour.empty())
  {
    return false;
  }

  std::vector<cv::Point2f> points;

  std::vector<cv::Point> approx;
  const double perimeter = cv::arcLength(contour, true);
  cv::approxPolyDP(contour, approx, 0.02 * perimeter, true);
  if (approx.size() == 4)
  {
    points.reserve(4);
    for (const cv::Point &pt : approx)
    {
      points.push_back(
          cv::Point2f(static_cast<float>(pt.x), static_cast<float>(pt.y)));
    }
  }
  else
  {
    int tlIndex = 0;
    int trIndex = 0;
    int brIndex = 0;
    int blIndex = 0;
    float minSum = static_cast<float>(contour.front().x + contour.front().y);
    float maxSum = minSum;
    float minDiff = static_cast<float>(contour.front().x - contour.front().y);
    float maxDiff = minDiff;

    for (int i = 0; i < static_cast<int>(contour.size()); ++i)
    {
      const float sum = static_cast<float>(contour[i].x + contour[i].y);
      const float diff = static_cast<float>(contour[i].x - contour[i].y);
      if (sum < minSum)
      {
        minSum = sum;
        tlIndex = i;
      }
      if (sum > maxSum)
      {
        maxSum = sum;
        brIndex = i;
      }
      if (diff > maxDiff)
      {
        maxDiff = diff;
        trIndex = i;
      }
      if (diff < minDiff)
      {
        minDiff = diff;
        blIndex = i;
      }
    }

    points = {
        cv::Point2f(static_cast<float>(contour[tlIndex].x),
                    static_cast<float>(contour[tlIndex].y)),
        cv::Point2f(static_cast<float>(contour[trIndex].x),
                    static_cast<float>(contour[trIndex].y)),
        cv::Point2f(static_cast<float>(contour[brIndex].x),
                    static_cast<float>(contour[brIndex].y)),
        cv::Point2f(static_cast<float>(contour[blIndex].x),
                    static_cast<float>(contour[blIndex].y)),
    };
  }

  if (points.size() != 4)
  {
    return false;
  }

  *quadOut = orderQuadPoints(points);
  return hasDistinctQuadPoints(*quadOut);
}

bool validateQuadGeometry(const std::array<cv::Point2f, 4> &quad,
                          double *angleOut, double *areaOut,
                          QString *rejectReasonOut)
{
  if (angleOut)
  {
    *angleOut = 0.0;
  }
  if (areaOut)
  {
    *areaOut = 0.0;
  }
  if (rejectReasonOut)
  {
    rejectReasonOut->clear();
  }

  const double dx = quad[1].x - quad[0].x;
  const double dy = quad[1].y - quad[0].y;
  const double angle = std::abs(std::atan2(dy, dx) * 180.0 / CV_PI);
  if (angleOut)
  {
    *angleOut = angle;
  }

  std::vector<cv::Point> quadInt;
  quadInt.reserve(4);
  for (const cv::Point2f &pt : quad)
  {
    quadInt.push_back(cv::Point(static_cast<int>(std::lround(pt.x)),
                                static_cast<int>(std::lround(pt.y))));
  }

  const bool isConvex = cv::isContourConvex(quadInt);
  const double area = std::abs(cv::contourArea(quadInt));
  if (areaOut)
  {
    *areaOut = area;
  }

  if (angle > kMaxTopEdgeAngleDeg)
  {
    if (rejectReasonOut)
    {
      *rejectReasonOut = QStringLiteral("top edge angle too large");
    }
    return false;
  }

  if (!isConvex)
  {
    if (rejectReasonOut)
    {
      *rejectReasonOut = QStringLiteral("quad is not convex");
    }
    return false;
  }

  if (area < kMinQuadArea)
  {
    if (rejectReasonOut)
    {
      *rejectReasonOut = QStringLiteral("quad area too small");
    }
    return false;
  }

  return true;
}

cv::Mat warpPlatePerspective(const cv::Mat &rgb,
                             const std::array<cv::Point2f, 4> &quad,
                             const int inputWidth, const int inputHeight)
{
  if (rgb.empty())
  {
    return cv::Mat();
  }

  const double topWidth = cv::norm(quad[1] - quad[0]);
  const double bottomWidth = cv::norm(quad[2] - quad[3]);
  const double leftHeight = cv::norm(quad[3] - quad[0]);
  const double rightHeight = cv::norm(quad[2] - quad[1]);

  const double avgWidth = std::max(1.0, (topWidth + bottomWidth) * 0.5);
  const double avgHeight = std::max(1.0, (leftHeight + rightHeight) * 0.5);

  double aspect = avgWidth / avgHeight;
  if (!std::isfinite(aspect) || aspect <= 0.0)
  {
    aspect = 4.0;
  }

  const int targetH = std::max(16, inputHeight > 0 ? inputHeight : kDefaultInputHeight);
  const int maxTargetW =
      std::max(kMinWarpWidth, inputWidth > 0 ? inputWidth : kDefaultInputWidth);
  const int targetW = std::clamp(static_cast<int>(std::lround(targetH * aspect)),
                                 kMinWarpWidth, maxTargetW);

  const std::array<cv::Point2f, 4> dst = {
      cv::Point2f(0.0f, 0.0f),
      cv::Point2f(static_cast<float>(targetW - 1), 0.0f),
      cv::Point2f(static_cast<float>(targetW - 1), static_cast<float>(targetH - 1)),
      cv::Point2f(0.0f, static_cast<float>(targetH - 1)),
  };

  cv::Mat transform = cv::getPerspectiveTransform(quad.data(), dst.data());
  cv::Mat warped;
  cv::warpPerspective(rgb, warped, transform, cv::Size(targetW, targetH),
                      cv::INTER_CUBIC, cv::BORDER_REPLICATE);
  return warped;
}

} // namespace

OcrManager::OcrManager() = default;

OcrManager::~OcrManager() = default;

QString OcrManager::findFirstFileRecursively(const QString &rootPath,
                                             const QStringList &filters)
{
  const QString trimmedRoot = rootPath.trimmed();
  if (trimmedRoot.isEmpty() || filters.isEmpty())
  {
    return QString();
  }

  const QFileInfo rootInfo(trimmedRoot);
  if (!rootInfo.exists() || !rootInfo.isDir())
  {
    return QString();
  }

  for (const QString &filter : filters)
  {
    QDirIterator it(rootInfo.absoluteFilePath(), QStringList(filter), QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext())
    {
      return QFileInfo(it.next()).absoluteFilePath();
    }
  }

  return QString();
}

QString OcrManager::resolveFilePath(const QString &pathOrDir,
                                    const QStringList &filters)
{
  const QString candidate = pathOrDir.trimmed();
  if (candidate.isEmpty())
  {
    return QString();
  }

  QFileInfo info(candidate);
  if (!info.exists() && info.isRelative())
  {
    const QDir currentDir(QDir::currentPath());
    info = QFileInfo(currentDir.absoluteFilePath(candidate));
  }

  if (info.exists() && info.isFile())
  {
    return info.absoluteFilePath();
  }

  if (info.exists() && info.isDir())
  {
    return findFirstFileRecursively(info.absoluteFilePath(), filters);
  }

  return QString();
}

QString OcrManager::resolveModelPath(const QString &modelPath) const
{
  const QString explicitPath =
      resolveFilePath(modelPath, QStringList{QStringLiteral("*.onnx")});
  if (!explicitPath.isEmpty())
  {
    return explicitPath;
  }

  const QString downloadsDir = QDir::home().filePath(QStringLiteral("Downloads"));
  return resolveFilePath(downloadsDir, QStringList{QStringLiteral("*.onnx")});
}

QString OcrManager::resolveDictionaryPath(const QString &dictPath,
                                          const QString &resolvedModelPath) const
{
  const QString explicitPath =
      resolveFilePath(dictPath, QStringList{QStringLiteral("dict.txt"),
                                            QStringLiteral("*.txt")});
  if (!explicitPath.isEmpty())
  {
    return explicitPath;
  }

  const QFileInfo modelInfo(resolvedModelPath);
  if (!resolvedModelPath.isEmpty() && modelInfo.exists())
  {
    const QString modelDirPath = modelInfo.absoluteDir().absolutePath();
    const QString nearModel =
        resolveFilePath(modelDirPath, QStringList{QStringLiteral("dict.txt"),
                                                  QStringLiteral("*.txt")});
    if (!nearModel.isEmpty())
    {
      return nearModel;
    }
  }

  const QString downloadsDir = QDir::home().filePath(QStringLiteral("Downloads"));
  return resolveFilePath(downloadsDir, QStringList{QStringLiteral("dict.txt"),
                                                   QStringLiteral("*.txt")});
}

bool OcrManager::init(const QString &modelPath, const QString &dictPath,
                      const int inputWidth, const int inputHeight)
{
  m_inputWidth = (inputWidth > 0) ? inputWidth : kDefaultInputWidth;
  m_inputHeight = (inputHeight > 0) ? inputHeight : kDefaultInputHeight;

  const QString resolvedModelPath = resolveModelPath(modelPath);
  const QString resolvedDictPath =
      resolveDictionaryPath(dictPath, resolvedModelPath);

  qDebug() << "[OCR] Initializing PaddleOCR. model=" << resolvedModelPath
           << "dict="
           << (resolvedDictPath.isEmpty() ? QStringLiteral("<builtin>")
                                          : resolvedDictPath)
           << "input=" << m_inputWidth << "x" << m_inputHeight;

  if (resolvedModelPath.isEmpty())
  {
    qDebug() << "[OCR] Failed to resolve ONNX model path from:" << modelPath;
    return false;
  }

  const QFileInfo modelInfo(resolvedModelPath);
  if (!modelInfo.exists() || !modelInfo.isFile())
  {
    qDebug() << "[OCR] ONNX model file not found:" << resolvedModelPath;
    return false;
  }

  if (resolvedDictPath.isEmpty() && !dictPath.trimmed().isEmpty())
  {
    qDebug() << "[OCR] Dictionary path could not be resolved, using built-in fallback:"
             << dictPath;
  }

  QString error;
  if (!m_runner.init(resolvedModelPath, resolvedDictPath, m_inputWidth,
                     m_inputHeight, &error))
  {
    qDebug() << "Could not initialize PaddleOCR:" << error;
    return false;
  }

  return true;
}

OcrResult OcrManager::performOcrDetailed(const QImage &image)
{
  OcrResult out;
  if (image.isNull())
  {
    out.dropReason = QStringLiteral("empty input image");
    return out;
  }

  const QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);
  cv::Mat roiView(formattedImage.height(), formattedImage.width(), CV_8UC3,
                  const_cast<uchar *>(formattedImage.constBits()),
                  formattedImage.bytesPerLine());

  const cv::Mat roiRgb = toRgbImage(roiView);
  if (roiRgb.empty())
  {
    out.dropReason = QStringLiteral("preprocess failed");
    return out;
  }

  cv::Mat normalizedRgb = roiRgb.clone();
  const cv::Mat gray = toGrayImage(roiRgb);
  if (gray.empty())
  {
    out.dropReason = QStringLiteral("preprocess failed");
    return out;
  }

  std::vector<cv::Point> plateContour;
  int contourCount = 0;
  double contourArea = 0.0;
  double topAngleDeg = 0.0;
  double quadArea = 0.0;
  QString rejectReason;
  bool warped = false;

  if (detectPlateContour(gray, &plateContour, &contourCount, &contourArea))
  {
    std::array<cv::Point2f, 4> quad{};
    if (extractQuadPoints(plateContour, &quad))
    {
      if (validateQuadGeometry(quad, &topAngleDeg, &quadArea, &rejectReason))
      {
        const cv::Mat warpedRgb =
            warpPlatePerspective(roiRgb, quad, m_inputWidth, m_inputHeight);
        if (!warpedRgb.empty())
        {
          normalizedRgb = warpedRgb;
          warped = true;
        }
        else
        {
          rejectReason = QStringLiteral("warpPerspective failed");
        }
      }
    }
    else
    {
      rejectReason = QStringLiteral("failed to extract quad");
    }
  }
  else
  {
    rejectReason = (contourCount > 0)
                       ? QStringLiteral("no contour above area threshold")
                       : QStringLiteral("no contours detected");
  }

  if (warped)
  {
    qDebug() << "[OCR] Perspective warp applied. contours=" << contourCount
             << "contourArea=" << contourArea << "quadArea=" << quadArea
             << "angle=" << topAngleDeg << "size=" << normalizedRgb.cols
             << "x" << normalizedRgb.rows;
  }
  else
  {
    qDebug() << "[OCR] Perspective warp skipped. contours=" << contourCount
             << "contourArea=" << contourArea << "reason=" << rejectReason;
  }

  double upscaleFactor = 1.0;
  double preTrimSharpness = 0.0;
  const cv::Mat upscaledRgb = upscaleForOcr(normalizedRgb, &upscaleFactor,
                                            &preTrimSharpness);
  if (!upscaledRgb.empty())
  {
    normalizedRgb = upscaledRgb;
    if (upscaleFactor > 1.05)
    {
      qDebug() << "[OCR] OCR upscale applied. scale=" << upscaleFactor
               << "sharpness=" << preTrimSharpness << "size="
               << normalizedRgb.cols << "x" << normalizedRgb.rows;
    }
  }

  cv::Rect trimmedRect;
  const cv::Mat trimmedRgb = trimPlateToContent(normalizedRgb, &trimmedRect);
  if (!trimmedRgb.empty())
  {
    if (!trimmedRect.empty())
    {
      qDebug() << "[OCR] Content trim applied. rect=" << trimmedRect.x
               << trimmedRect.y << trimmedRect.width << trimmedRect.height
               << "size=" << trimmedRgb.cols << "x" << trimmedRgb.rows;
    }
    else
    {
      qDebug() << "[OCR] Content trim skipped. size=" << trimmedRgb.cols << "x"
               << trimmedRgb.rows;
    }
    normalizedRgb = trimmedRgb;
  }

  cv::Mat enhancedGray;
  std::vector<ocr::preprocess::OcrInputVariant> variants;
  if (!buildOcrVariants(normalizedRgb, &enhancedGray, &variants))
  {
    out.dropReason = QStringLiteral("preprocess failed");
    return out;
  }

  ocr::debug::dumpOcrStages(roiRgb, normalizedRgb, enhancedGray, variants);

  if (!m_runner.isReady())
  {
    out.dropReason = QStringLiteral("ocr runner not initialized");
    return out;
  }

  const std::vector<ocr::postprocess::OcrCandidate> candidates =
      m_runner.collectCandidates(variants);
  const OcrResult result = ocr::postprocess::chooseBestPlateResult(candidates);
  qDebug() << "[OCR][Final] raw=" << result.selectedRawText
           << "text=" << result.text
           << "selected=" << result.selectedCandidate
           << "score=" << result.selectedScore
           << "confidence=" << result.selectedConfidence
           << "dropReason=" << result.dropReason;
  return result;
}

QString OcrManager::performOcr(const QImage &image)
{
  return performOcrDetailed(image).text;
}
