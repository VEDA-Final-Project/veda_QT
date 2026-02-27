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

  cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.4, cv::Size(8, 8));
  cv::Mat out;
  clahe->apply(gray, out);
  return out;
}

cv::Mat unsharpMask(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  cv::Mat blurred;
  cv::GaussianBlur(gray, blurred, cv::Size(0, 0), 1.2);
  cv::Mat sharpened;
  cv::addWeighted(gray, 1.55, blurred, -0.55, 0.0, sharpened);
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

cv::Mat otsuBinary(const cv::Mat &gray)
{
  if (gray.empty())
  {
    return cv::Mat();
  }

  cv::Mat binary;
  cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
  return binary;
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

  const cv::Mat claheGray = applyClahe(gray);
  const cv::Mat sharpGray = unsharpMask(claheGray.empty() ? gray : claheGray);
  const cv::Mat contrastGray =
      contrastStretch(claheGray.empty() ? gray : claheGray);
  const cv::Mat binaryGray =
      otsuBinary(contrastGray.empty() ? (claheGray.empty() ? gray : claheGray)
                                      : contrastGray);

  *enhancedGrayOut = sharpGray.empty() ? gray.clone() : sharpGray.clone();

  appendVariant(variantsOut, QStringLiteral("rgb_base"), rgb);
  appendVariant(variantsOut, QStringLiteral("gray_clahe"),
                grayToRgb(claheGray.empty() ? gray : claheGray));
  appendVariant(variantsOut, QStringLiteral("gray_sharp"),
                grayToRgb(sharpGray.empty() ? gray : sharpGray));
  appendVariant(variantsOut, QStringLiteral("gray_contrast"),
                grayToRgb(contrastGray.empty() ? gray : contrastGray));
  appendVariant(variantsOut, QStringLiteral("gray_binary"),
                grayToRgb(binaryGray.empty() ? gray : binaryGray));

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
