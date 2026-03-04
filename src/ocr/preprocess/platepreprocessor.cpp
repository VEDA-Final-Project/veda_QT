#include "ocr/preprocess/platepreprocessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace ocr::preprocess
{
namespace
{
constexpr int kDefaultInputWidth = 320;
constexpr int kDefaultInputHeight = 48;
constexpr int kMinWarpWidth = 96;
constexpr double kMinContourArea = 280.0;
constexpr double kMinQuadArea = 220.0;
constexpr double kMinContourAspect = 1.6;
constexpr double kMaxContourAspect = 10.5;
constexpr double kMinRectangularity = 0.32;
constexpr double kMaxContourBoundsAreaRatio = 0.995;
constexpr double kBorderPenaltyBoundsAreaRatio = 0.96;
constexpr double kBorderPenaltyScore = 1.4;
constexpr double kMaxTopEdgeAngleDeg = 60.0;
constexpr double kLowQualitySharpness = 80.0;

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

double estimateIntensityStdDev(const cv::Mat &gray)
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

double estimateDynamicRange(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return 0.0;
  }

  double minVal = 0.0;
  double maxVal = 0.0;
  cv::minMaxLoc(gray, &minVal, &maxVal);
  return maxVal - minVal;
}

bool shouldApplyClahe(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return false;
  }

  const double intensityStdDev = estimateIntensityStdDev(gray);
  const double dynamicRange = estimateDynamicRange(gray);
  return (intensityStdDev < 52.0 || dynamicRange < 170.0);
}

cv::Mat upscaleForOcr(const cv::Mat &rgb, double *scaleOut)
{
  if (scaleOut)
  {
    *scaleOut = 1.0;
  }

  const cv::Mat gray = toGrayImage(rgb);
  if (gray.empty())
  {
    return rgb.clone();
  }

  const double sharpness = estimateSharpness(gray);

  double scale = 1.0;
  if (rgb.cols < 60 || rgb.rows < 16)
  {
    scale = 1.35;
  }
  else if ((rgb.cols < 84 || rgb.rows < 22) && sharpness < 70.0)
  {
    scale = 1.2;
  }

  if (sharpness < 20.0 && (rgb.cols < 72 || rgb.rows < 20))
  {
    scale = std::max(scale, 1.45);
  }
  scale = std::clamp(scale, 1.0, 1.5);
  if (scale <= 1.05)
  {
    return rgb.clone();
  }

  cv::Mat upscaled;
  cv::resize(rgb, upscaled, cv::Size(), scale, scale, cv::INTER_LINEAR);
  if (scaleOut)
  {
    *scaleOut = scale;
  }
  return upscaled;
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

std::vector<int> smoothProjection(const std::vector<int> &values, int radius)
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

bool findProjectionRange(const std::vector<int> &values, int threshold,
                         int startIndex, int endIndex, int *firstOut,
                         int *lastOut)
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

  const int colThreshold = std::max(
      2, static_cast<int>(std::lround(gray.rows * (lowQuality ? 0.16 : 0.12))));
  const int rowThreshold = std::max(
      2, static_cast<int>(std::lround(gray.cols * (lowQuality ? 0.11 : 0.08))));

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
    const cv::Rect projectedRect(left, top, (right - left) + 1,
                                 (bottom - top) + 1);
    if (projectedRect.width >= static_cast<int>(std::lround(gray.cols * 0.45)) &&
        projectedRect.height >= static_cast<int>(std::lround(gray.rows * 0.28)))
    {
      const int padX = std::max(
          3, static_cast<int>(std::lround(projectedRect.width *
                                          (lowQuality ? 0.09 : 0.08))));
      const int padY = std::max(
          3, static_cast<int>(std::lround(projectedRect.height * 0.15)));
      const cv::Rect expanded = clampRectToImage(
          cv::Rect(projectedRect.x - padX, projectedRect.y - padY,
                   projectedRect.width + (padX * 2),
                   projectedRect.height + (padY * 2)),
          gray.size());
      if (expanded.width >= static_cast<int>(std::lround(
                                gray.cols * (lowQuality ? 0.72 : 0.60))) &&
          expanded.height >= static_cast<int>(std::lround(
                                 gray.rows * (lowQuality ? 0.70 : 0.55))) &&
          (expanded.width < (gray.cols - 2) ||
           expanded.height < (gray.rows - 2)))
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
          (tightExpanded.width < (gray.cols - 2) ||
           tightExpanded.height < (gray.rows - 2)))
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
        (x <= borderX || y <= borderY || (x + w) >= (gray.cols - borderX) ||
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

  const int padX = std::max(
      4, static_cast<int>(std::lround(contentRect.width *
                                      (lowQuality ? 0.16 : 0.10))));
  const int padY = std::max(
      3, static_cast<int>(std::lround(contentRect.height *
                                      (lowQuality ? 0.24 : 0.18))));
  const cv::Rect expanded = clampRectToImage(
      cv::Rect(contentRect.x - padX, contentRect.y - padY,
               contentRect.width + (padX * 2), contentRect.height + (padY * 2)),
      gray.size());

  if (expanded.width < static_cast<int>(std::lround(
                            gray.cols * (lowQuality ? 0.72 : 0.50))) ||
      expanded.height < static_cast<int>(std::lround(
                             gray.rows * (lowQuality ? 0.68 : 0.50))))
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

  const int padX =
      std::max(2, static_cast<int>(std::lround(gray.cols * 0.035)));
  const int padTop =
      std::max(2, static_cast<int>(std::lround(gray.rows * 0.10)));
  const int padBottom =
      std::max(2, static_cast<int>(std::lround(gray.rows * 0.08)));
  return clampRectToImage(
      cv::Rect(padX, padTop, std::max(1, gray.cols - (padX * 2)),
               std::max(1, gray.rows - padTop - padBottom)),
      gray.size());
}

bool buildOcrInput(const cv::Mat &normalizedRgb, cv::Mat *enhancedGrayOut,
                   cv::Mat *ocrInputRgbOut)
{
  if (!enhancedGrayOut || !ocrInputRgbOut)
  {
    return false;
  }

  *enhancedGrayOut = cv::Mat();
  *ocrInputRgbOut = cv::Mat();

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

  cv::Mat ocrGray = gray.clone();
  if (shouldApplyClahe(gray))
  {
    const cv::Mat claheGray = applyClahe(gray);
    if (!claheGray.empty())
    {
      ocrGray = claheGray;
    }
  }

  *enhancedGrayOut = ocrGray.clone();
  *ocrInputRgbOut = grayToRgb(ocrGray);
  return !ocrInputRgbOut->empty();
}

bool detectPlateContour(const cv::Mat &gray, std::vector<cv::Point> *contourOut)
{
  if (contourOut)
  {
    contourOut->clear();
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

  const double imageArea = static_cast<double>(std::max(1, gray.cols * gray.rows));
  double bestScore = -1.0;
  double bestFallbackArea = 0.0;
  std::vector<cv::Point> fallbackContour;

  for (const std::vector<cv::Point> &contour : contours)
  {
    const double area = std::abs(cv::contourArea(contour));
    if (area < kMinContourArea)
    {
      continue;
    }

    if (area > bestFallbackArea)
    {
      bestFallbackArea = area;
      fallbackContour = contour;
    }

    const cv::Rect bounds = cv::boundingRect(contour);
    const double boundsArea =
        static_cast<double>(std::max(1, bounds.width * bounds.height));
    if (boundsArea >= (imageArea * kMaxContourBoundsAreaRatio))
    {
      continue;
    }

    const cv::RotatedRect minRect = cv::minAreaRect(contour);
    const double rectWidth =
        std::max(1.0, static_cast<double>(minRect.size.width));
    const double rectHeight =
        std::max(1.0, static_cast<double>(minRect.size.height));
    const double rectArea = rectWidth * rectHeight;
    if (rectArea < kMinQuadArea)
    {
      continue;
    }

    const double aspect = std::max(rectWidth, rectHeight) /
                          std::max(1.0, std::min(rectWidth, rectHeight));
    if (aspect < kMinContourAspect || aspect > kMaxContourAspect)
    {
      continue;
    }

    const double rectangularity = area / rectArea;
    if (rectangularity < kMinRectangularity)
    {
      continue;
    }

    std::vector<cv::Point> approx;
    const double perimeter = cv::arcLength(contour, true);
    cv::approxPolyDP(contour, approx, 0.02 * perimeter, true);

    const bool likelyQuad = (approx.size() == 4 && cv::isContourConvex(approx));
    const double areaScore = std::min(0.95, area / imageArea) * 6.0;
    const double rectScore = rectangularity * 2.0;
    const double quadBonus = likelyQuad ? 2.5 : 0.0;
    const double borderPenalty =
        (boundsArea > (imageArea * kBorderPenaltyBoundsAreaRatio))
            ? kBorderPenaltyScore
            : 0.0;
    const double score = areaScore + rectScore + quadBonus - borderPenalty;

    if (score > bestScore)
    {
      bestScore = score;
      *contourOut = contour;
    }
  }

  if (contourOut->empty() && !fallbackContour.empty())
  {
    *contourOut = fallbackContour;
  }
  return !contourOut->empty();
}

std::array<cv::Point2f, 4> orderQuadPoints(
    const std::vector<cv::Point2f> &points)
{
  std::array<cv::Point2f, 4> ordered{};
  if (points.size() != 4)
  {
    return ordered;
  }

  auto sumOf = [](const cv::Point2f &pt) { return pt.x + pt.y; };
  auto diffOf = [](const cv::Point2f &pt) { return pt.x - pt.y; };

  ordered[0] = *std::min_element(
      points.begin(), points.end(),
      [&](const cv::Point2f &lhs, const cv::Point2f &rhs) {
        return sumOf(lhs) < sumOf(rhs);
      });
  ordered[2] = *std::max_element(
      points.begin(), points.end(),
      [&](const cv::Point2f &lhs, const cv::Point2f &rhs) {
        return sumOf(lhs) < sumOf(rhs);
      });
  ordered[1] = *std::max_element(
      points.begin(), points.end(),
      [&](const cv::Point2f &lhs, const cv::Point2f &rhs) {
        return diffOf(lhs) < diffOf(rhs);
      });
  ordered[3] = *std::min_element(
      points.begin(), points.end(),
      [&](const cv::Point2f &lhs, const cv::Point2f &rhs) {
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

  const double perimeter = cv::arcLength(contour, true);
  for (const double epsilonScale : {0.018, 0.03, 0.045})
  {
    std::vector<cv::Point> approx;
    cv::approxPolyDP(contour, approx, epsilonScale * perimeter, true);
    if (approx.size() != 4 || !cv::isContourConvex(approx))
    {
      continue;
    }

    points.reserve(4);
    for (const cv::Point &pt : approx)
    {
      points.push_back(
          cv::Point2f(static_cast<float>(pt.x), static_cast<float>(pt.y)));
    }
    break;
  }

  if (points.empty())
  {
    const cv::RotatedRect rect = cv::minAreaRect(contour);
    if (std::min(rect.size.width, rect.size.height) < 4.0f)
    {
      return false;
    }

    std::array<cv::Point2f, 4> rectPoints{};
    rect.points(rectPoints.data());
    points.assign(rectPoints.begin(), rectPoints.end());
  }

  if (points.size() != 4)
  {
    return false;
  }

  *quadOut = orderQuadPoints(points);
  return hasDistinctQuadPoints(*quadOut);
}

bool validateQuadGeometry(const std::array<cv::Point2f, 4> &quad)
{
  const double dx = quad[1].x - quad[0].x;
  const double dy = quad[1].y - quad[0].y;
  const double angle = std::abs(std::atan2(dy, dx) * 180.0 / CV_PI);

  std::vector<cv::Point> quadInt;
  quadInt.reserve(4);
  for (const cv::Point2f &pt : quad)
  {
    quadInt.push_back(cv::Point(static_cast<int>(std::lround(pt.x)),
                                static_cast<int>(std::lround(pt.y))));
  }

  const bool isConvex = cv::isContourConvex(quadInt);
  const double area = std::abs(cv::contourArea(quadInt));

  if (angle > kMaxTopEdgeAngleDeg)
  {
    return false;
  }

  if (!isConvex)
  {
    return false;
  }

  if (area < kMinQuadArea)
  {
    return false;
  }

  return true;
}

bool shouldWarpPerspective(const std::array<cv::Point2f, 4> &quad)
{
  const double topWidth = cv::norm(quad[1] - quad[0]);
  const double bottomWidth = cv::norm(quad[2] - quad[3]);
  const double leftHeight = cv::norm(quad[3] - quad[0]);
  const double rightHeight = cv::norm(quad[2] - quad[1]);

  const double minWidth = std::max(1.0, std::min(topWidth, bottomWidth));
  const double minHeight = std::max(1.0, std::min(leftHeight, rightHeight));
  const double widthRatio = std::max(topWidth, bottomWidth) / minWidth;
  const double heightRatio = std::max(leftHeight, rightHeight) / minHeight;

  const double topAngle = std::abs(
      std::atan2(quad[1].y - quad[0].y, quad[1].x - quad[0].x) * 180.0 / CV_PI);
  const double bottomAngle = std::abs(
      std::atan2(quad[2].y - quad[3].y, quad[2].x - quad[3].x) * 180.0 / CV_PI);

  const bool hasVisibleTilt = (topAngle > 7.0 || bottomAngle > 7.0);
  const bool hasPerspectiveSkew = (widthRatio > 1.12 || heightRatio > 1.12);
  return hasVisibleTilt || hasPerspectiveSkew;
}

bool extractDeskewQuadFromCrop(const cv::Mat &gray,
                               std::array<cv::Point2f, 4> *quadOut)
{
  if (!quadOut || gray.empty())
  {
    return false;
  }

  const cv::Mat denoised = denoiseGray(gray);
  const cv::Mat stretched = contrastStretch(denoised.empty() ? gray : denoised);
  const cv::Mat base = stretched.empty() ? gray : stretched;

  cv::Mat edges;
  cv::Canny(base, edges, 40.0, 120.0);
  cv::morphologyEx(edges, edges, cv::MORPH_CLOSE,
                   cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
  cv::dilate(edges, edges, cv::Mat(), cv::Point(-1, -1), 1);

  if (edges.cols > 2 && edges.rows > 2)
  {
    edges.row(0).setTo(0);
    edges.row(edges.rows - 1).setTo(0);
    edges.col(0).setTo(0);
    edges.col(edges.cols - 1).setTo(0);
  }

  std::vector<cv::Point> points;
  cv::findNonZero(edges, points);
  if (points.size() < 24)
  {
    return false;
  }

  const cv::RotatedRect rect = cv::minAreaRect(points);
  const double rectWidth = std::max(1.0f, rect.size.width);
  const double rectHeight = std::max(1.0f, rect.size.height);
  const double rectArea = rectWidth * rectHeight;
  const double imageArea =
      static_cast<double>(std::max(1, gray.cols * gray.rows));
  if (rectArea < kMinQuadArea || rectArea < (imageArea * 0.22))
  {
    return false;
  }

  std::array<cv::Point2f, 4> rectPoints{};
  rect.points(rectPoints.data());
  *quadOut = orderQuadPoints(
      std::vector<cv::Point2f>(rectPoints.begin(), rectPoints.end()));
  return hasDistinctQuadPoints(*quadOut);
}

cv::Mat warpPlatePerspective(const cv::Mat &rgb,
                             const std::array<cv::Point2f, 4> &quad,
                             int inputWidth, int inputHeight)
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

  const int targetH =
      std::max(16, inputHeight > 0 ? inputHeight : kDefaultInputHeight);
  const int maxTargetW =
      std::max(kMinWarpWidth, inputWidth > 0 ? inputWidth : kDefaultInputWidth);
  const int targetW = std::clamp(static_cast<int>(std::lround(targetH * aspect)),
                                 kMinWarpWidth, maxTargetW);

  const std::array<cv::Point2f, 4> dst = {
      cv::Point2f(0.0f, 0.0f),
      cv::Point2f(static_cast<float>(targetW - 1), 0.0f),
      cv::Point2f(static_cast<float>(targetW - 1),
                  static_cast<float>(targetH - 1)),
      cv::Point2f(0.0f, static_cast<float>(targetH - 1)),
  };

  cv::Mat transform = cv::getPerspectiveTransform(quad.data(), dst.data());
  cv::Mat warped;
  cv::warpPerspective(rgb, warped, transform, cv::Size(targetW, targetH),
                      cv::INTER_CUBIC, cv::BORDER_REPLICATE);
  return warped;
}

} // namespace

bool preprocessPlateImage(const QImage &image, const int inputWidth,
                          const int inputHeight,
                          PlatePreprocessResult *resultOut)
{
  if (!resultOut)
  {
    return false;
  }

  *resultOut = PlatePreprocessResult{};

  if (image.isNull())
  {
    return false;
  }

  const QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);
  cv::Mat roiView(formattedImage.height(), formattedImage.width(), CV_8UC3,
                  const_cast<uchar *>(formattedImage.constBits()),
                  formattedImage.bytesPerLine());

  resultOut->roiRgb = toRgbImage(roiView);
  if (resultOut->roiRgb.empty())
  {
    return false;
  }

  resultOut->normalizedRgb = resultOut->roiRgb.clone();
  const cv::Mat gray = toGrayImage(resultOut->normalizedRgb);
  if (gray.empty())
  {
    return false;
  }

  std::array<cv::Point2f, 4> quad{};
  if (extractDeskewQuadFromCrop(gray, &quad) && validateQuadGeometry(quad) &&
      shouldWarpPerspective(quad))
  {
    const cv::Mat warpedRgb =
        warpPlatePerspective(resultOut->normalizedRgb, quad, inputWidth, inputHeight);
    if (!warpedRgb.empty())
    {
      resultOut->normalizedRgb = warpedRgb;
    }
  }

  // Metadata already provides a LicensePlate crop, so keep straight plates
  // intact and only restore lightweight OCR-oriented enhancement otherwise.
  double upscaleFactor = 1.0;
  const cv::Mat upscaledRgb =
      upscaleForOcr(resultOut->normalizedRgb, &upscaleFactor);
  if (!upscaledRgb.empty())
  {
    resultOut->normalizedRgb = upscaledRgb;
  }

  if (!buildOcrInput(resultOut->normalizedRgb, &resultOut->enhancedGray,
                     &resultOut->ocrInputRgb))
  {
    return false;
  }

  return true;
}

} // namespace ocr::preprocess
