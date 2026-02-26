#include "ocr/preprocess/platepreprocessor.h"
#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ocr::preprocess
{
  namespace
  {
    constexpr double kOcrUpscaleFactor = 2.0;
    constexpr int kPerspectiveMaxContours = 35;
    constexpr double kPerspectiveMinAreaRatio = 0.04;
    constexpr double kPerspectiveMaxAreaRatio = 0.95;
    constexpr double kPerspectiveMinAspect = 1.7;
    constexpr double kPerspectiveMaxAspect = 7.2;
    constexpr double kPerspectiveMinRectFill = 0.42;
    constexpr int kPerspectiveMinRawWidth = 30;
    constexpr int kPerspectiveMinRawHeight = 10;
    constexpr int kPerspectiveMinDstWidth = 160;
    constexpr int kPerspectiveMinDstHeight = 48;
    constexpr double kPerspectiveMinAcceptScore = 95.0;
    constexpr double kInvalidCandidateScore = -1e18;
    constexpr int kAdaptiveBlockSize = 31;
    constexpr int kAdaptiveC = 7;
    constexpr double kAdaptiveWhiteRatioLo = 0.18;
    constexpr double kAdaptiveWhiteRatioHi = 0.82;

    struct PerspectiveCandidate
    {
      std::vector<cv::Point2f> quad;
      double score = kInvalidCandidateScore;
      int dstWidth = 0;
      int dstHeight = 0;
      bool fromMinAreaRect = false;
    };

    struct PerspectiveSearchResult
    {
      PerspectiveCandidate best;
      bool hasCandidate = false;
      bool accepted = false;
    };

    bool shouldUseAdaptiveThreshold(const cv::Mat &otsuBinary)
    {
      if (otsuBinary.empty() || otsuBinary.type() != CV_8UC1)
      {
        return false;
      }

      const QString forceAdaptive =
          qEnvironmentVariable("OCR_FORCE_ADAPTIVE").trimmed().toLower();
      if (forceAdaptive == QStringLiteral("1") ||
          forceAdaptive == QStringLiteral("true") ||
          forceAdaptive == QStringLiteral("yes"))
      {
        return true;
      }
      if (forceAdaptive == QStringLiteral("0") ||
          forceAdaptive == QStringLiteral("false") ||
          forceAdaptive == QStringLiteral("no"))
      {
        return false;
      }

      const double total = static_cast<double>(otsuBinary.rows) * otsuBinary.cols;
      if (total <= 0.0)
      {
        return false;
      }

      const double whiteRatio =
          static_cast<double>(cv::countNonZero(otsuBinary)) / total;
      return (whiteRatio < kAdaptiveWhiteRatioLo ||
              whiteRatio > kAdaptiveWhiteRatioHi);
    }

    double clamp01(const double value)
    {
      return std::clamp(value, 0.0, 1.0);
    }

    std::vector<cv::Point2f> clampQuadToImage(const std::vector<cv::Point2f> &quad,
                                              const cv::Size &size)
    {
      if (quad.size() != 4 || size.width <= 0 || size.height <= 0)
      {
        return {};
      }

      std::vector<cv::Point2f> out;
      out.reserve(4);
      for (const cv::Point2f &pt : quad)
      {
        const float x = std::clamp(pt.x, 0.0f, static_cast<float>(size.width - 1));
        const float y = std::clamp(pt.y, 0.0f, static_cast<float>(size.height - 1));
        out.emplace_back(x, y);
      }
      return out;
    }

    std::vector<cv::Point2f> orderQuadPoints(const std::vector<cv::Point2f> &quad)
    {
      if (quad.size() != 4)
      {
        return {};
      }

      int tl = 0;
      int tr = 0;
      int br = 0;
      int bl = 0;
      double minSum = std::numeric_limits<double>::max();
      double maxSum = std::numeric_limits<double>::lowest();
      double minDiff = std::numeric_limits<double>::max();
      double maxDiff = std::numeric_limits<double>::lowest();

      for (int i = 0; i < 4; ++i)
      {
        const double sum = static_cast<double>(quad[i].x + quad[i].y);
        const double diff = static_cast<double>(quad[i].x - quad[i].y);

        if (sum < minSum)
        {
          minSum = sum;
          tl = i;
        }
        if (sum > maxSum)
        {
          maxSum = sum;
          br = i;
        }
        if (diff > maxDiff)
        {
          maxDiff = diff;
          tr = i;
        }
        if (diff < minDiff)
        {
          minDiff = diff;
          bl = i;
        }
      }

      std::vector<int> indexSet = {tl, tr, br, bl};
      std::sort(indexSet.begin(), indexSet.end());
      indexSet.erase(std::unique(indexSet.begin(), indexSet.end()), indexSet.end());
      if (indexSet.size() != 4)
      {
        return {};
      }

      return {quad[tl], quad[tr], quad[br], quad[bl]};
    }

    bool isUsableQuad(const std::vector<cv::Point2f> &quad)
    {
      if (quad.size() != 4)
      {
        return false;
      }

      std::vector<cv::Point> contour;
      contour.reserve(4);
      for (const cv::Point2f &pt : quad)
      {
        contour.emplace_back(cvRound(pt.x), cvRound(pt.y));
      }

      if (!cv::isContourConvex(contour))
      {
        return false;
      }

      return std::abs(cv::contourArea(quad)) > 10.0;
    }

    double estimateEdgeDensityInQuad(const cv::Mat &edges,
                                     const std::vector<cv::Point2f> &quad)
    {
      if (edges.empty() || edges.type() != CV_8UC1 || quad.size() != 4)
      {
        return 0.0;
      }

      std::vector<cv::Point> polygon;
      polygon.reserve(4);
      for (const cv::Point2f &pt : quad)
      {
        polygon.emplace_back(cvRound(pt.x), cvRound(pt.y));
      }

      cv::Mat mask = cv::Mat::zeros(edges.size(), CV_8UC1);
      cv::fillConvexPoly(mask, polygon, cv::Scalar(255), cv::LINE_AA);

      const double maskArea = static_cast<double>(cv::countNonZero(mask));
      if (maskArea <= 1.0)
      {
        return 0.0;
      }

      cv::Mat maskedEdges;
      cv::bitwise_and(edges, mask, maskedEdges);
      const double edgePixels = static_cast<double>(cv::countNonZero(maskedEdges));
      return edgePixels / maskArea;
    }

    double scorePerspectiveCandidate(const std::vector<cv::Point2f> &orderedQuad,
                                     const cv::Size &imageSize,
                                     const double contourAreaAbs,
                                     const double boundingArea,
                                     const cv::Mat &edges, int *dstWidthOut,
                                     int *dstHeightOut)
    {
      if (!isUsableQuad(orderedQuad) || imageSize.width <= 0 || imageSize.height <= 0)
      {
        return kInvalidCandidateScore;
      }

      const double quadArea = std::abs(cv::contourArea(orderedQuad));
      const double imageArea = static_cast<double>(imageSize.width) * imageSize.height;
      if (quadArea <= 1.0 || imageArea <= 1.0)
      {
        return kInvalidCandidateScore;
      }

      const double areaRatio = quadArea / imageArea;
      if (areaRatio < kPerspectiveMinAreaRatio || areaRatio > kPerspectiveMaxAreaRatio)
      {
        return kInvalidCandidateScore;
      }

      const double widthTop = cv::norm(orderedQuad[1] - orderedQuad[0]);
      const double widthBottom = cv::norm(orderedQuad[2] - orderedQuad[3]);
      const double heightLeft = cv::norm(orderedQuad[3] - orderedQuad[0]);
      const double heightRight = cv::norm(orderedQuad[2] - orderedQuad[1]);
      const double rawWidth = 0.5 * (widthTop + widthBottom);
      const double rawHeight = 0.5 * (heightLeft + heightRight);

      if (rawWidth < kPerspectiveMinRawWidth || rawHeight < kPerspectiveMinRawHeight)
      {
        return kInvalidCandidateScore;
      }

      const double aspect = rawWidth / std::max(1.0, rawHeight);
      if (aspect < kPerspectiveMinAspect || aspect > kPerspectiveMaxAspect)
      {
        return kInvalidCandidateScore;
      }

      const double rectFill = contourAreaAbs / std::max(1.0, boundingArea);
      if (rectFill < kPerspectiveMinRectFill)
      {
        return kInvalidCandidateScore;
      }

      int dstWidth = std::max(
          kPerspectiveMinDstWidth,
          static_cast<int>(std::lround(std::max(widthTop, widthBottom))));
      int dstHeight = std::max(
          kPerspectiveMinDstHeight,
          static_cast<int>(std::lround(std::max(heightLeft, heightRight))));

      dstWidth = std::clamp(dstWidth, kPerspectiveMinDstWidth,
                            std::max(kPerspectiveMinDstWidth, imageSize.width * 2));
      dstHeight = std::clamp(dstHeight, kPerspectiveMinDstHeight,
                             std::max(kPerspectiveMinDstHeight, imageSize.height * 2));

      if (dstWidthOut)
      {
        *dstWidthOut = dstWidth;
      }
      if (dstHeightOut)
      {
        *dstHeightOut = dstHeight;
      }

      const double edgeDensity = estimateEdgeDensityInQuad(edges, orderedQuad);
      const double areaScore =
          1.0 - std::min(1.0, std::abs(areaRatio - 0.22) / 0.22);
      const double aspectScore =
          1.0 - std::min(1.0, std::abs(aspect - 3.6) / 3.0);
      const double fillScore = clamp01(
          (rectFill - kPerspectiveMinRectFill) / (1.0 - kPerspectiveMinRectFill));
      const double edgeScore = std::min(1.0, edgeDensity * 9.5);

      return areaScore * 110.0 + aspectScore * 130.0 + fillScore * 70.0 +
             edgeScore * 130.0;
    }

    void considerPerspectiveCandidate(const std::vector<cv::Point2f> &rawQuad,
                                      const cv::Size &imageSize,
                                      const double contourAreaAbs,
                                      const double boundingArea,
                                      const cv::Mat &edges,
                                      const bool fromMinAreaRect,
                                      PerspectiveSearchResult *result)
    {
      if (!result)
      {
        return;
      }

      const std::vector<cv::Point2f> clamped = clampQuadToImage(rawQuad, imageSize);
      const std::vector<cv::Point2f> ordered = orderQuadPoints(clamped);
      if (!isUsableQuad(ordered))
      {
        return;
      }

      int dstWidth = 0;
      int dstHeight = 0;
      double score = scorePerspectiveCandidate(ordered, imageSize, contourAreaAbs,
                                               boundingArea, edges, &dstWidth,
                                               &dstHeight);
      if (score <= kInvalidCandidateScore / 2.0)
      {
        return;
      }

      if (fromMinAreaRect)
      {
        score -= 10.0;
      }

      if (!result->hasCandidate || score > result->best.score)
      {
        result->hasCandidate = true;
        result->best.quad = ordered;
        result->best.score = score;
        result->best.dstWidth = dstWidth;
        result->best.dstHeight = dstHeight;
        result->best.fromMinAreaRect = fromMinAreaRect;
      }
    }

    PerspectiveSearchResult findPerspectiveCandidate(const cv::Mat &gray)
    {
      PerspectiveSearchResult result;
      if (gray.empty())
      {
        return result;
      }

      cv::Mat blurred;
      cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0.0);

      cv::Mat edgesStrong;
      cv::Canny(blurred, edgesStrong, 60, 180);

      cv::Mat edgesSoft;
      cv::Canny(blurred, edgesSoft, 35, 120);

      cv::Mat edges;
      cv::bitwise_or(edgesStrong, edgesSoft, edges);

      cv::Mat closed;
      const cv::Mat closeKernel =
          cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 3));
      cv::morphologyEx(edges, closed, cv::MORPH_CLOSE, closeKernel);

      const cv::Mat dilateKernel =
          cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
      cv::dilate(closed, closed, dilateKernel, cv::Point(-1, -1), 1);

      std::vector<std::vector<cv::Point>> contours;
      cv::findContours(closed, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
      if (contours.empty())
      {
        return result;
      }

      std::sort(contours.begin(), contours.end(),
                [](const std::vector<cv::Point> &lhs,
                   const std::vector<cv::Point> &rhs)
                {
                  return std::abs(cv::contourArea(lhs)) >
                         std::abs(cv::contourArea(rhs));
                });

      if (static_cast<int>(contours.size()) > kPerspectiveMaxContours)
      {
        contours.resize(kPerspectiveMaxContours);
      }

      for (const std::vector<cv::Point> &contour : contours)
      {
        const double contourAreaAbs = std::abs(cv::contourArea(contour));
        if (contourAreaAbs < 20.0)
        {
          continue;
        }

        const cv::Rect bbox = cv::boundingRect(contour);
        if (bbox.width <= 0 || bbox.height <= 0)
        {
          continue;
        }
        const double boundingArea = static_cast<double>(bbox.area());

        const double perimeter = cv::arcLength(contour, true);
        if (perimeter > 0.0)
        {
          std::vector<cv::Point> approx;
          cv::approxPolyDP(contour, approx, 0.02 * perimeter, true);
          if (approx.size() == 4 && cv::isContourConvex(approx))
          {
            std::vector<cv::Point2f> approxQuad;
            approxQuad.reserve(4);
            for (const cv::Point &pt : approx)
            {
              approxQuad.emplace_back(static_cast<float>(pt.x),
                                      static_cast<float>(pt.y));
            }

            considerPerspectiveCandidate(approxQuad, gray.size(), contourAreaAbs,
                                         boundingArea, edges, false, &result);
          }
        }

        const cv::RotatedRect minRect = cv::minAreaRect(contour);
        cv::Point2f minRectPts[4];
        minRect.points(minRectPts);
        std::vector<cv::Point2f> minRectQuad(minRectPts, minRectPts + 4);
        considerPerspectiveCandidate(minRectQuad, gray.size(), contourAreaAbs,
                                     boundingArea, edges, true, &result);
      }

      result.accepted =
          result.hasCandidate && result.best.score >= kPerspectiveMinAcceptScore;
      return result;
    }

    bool applyPerspectiveCandidate(const cv::Mat &srcRgb, const cv::Mat &srcGray,
                                   const PerspectiveCandidate &candidate,
                                   cv::Mat *rectifiedRgb, cv::Mat *rectifiedGray)
    {
      if (!rectifiedRgb || !rectifiedGray || srcRgb.empty() || srcGray.empty() ||
          candidate.quad.size() != 4 || candidate.dstWidth <= 0 ||
          candidate.dstHeight <= 0)
      {
        return false;
      }

      std::vector<cv::Point2f> dstQuad = {
          cv::Point2f(0.0f, 0.0f),
          cv::Point2f(static_cast<float>(candidate.dstWidth - 1), 0.0f),
          cv::Point2f(static_cast<float>(candidate.dstWidth - 1),
                      static_cast<float>(candidate.dstHeight - 1)),
          cv::Point2f(0.0f, static_cast<float>(candidate.dstHeight - 1))};

      const cv::Mat transform = cv::getPerspectiveTransform(candidate.quad, dstQuad);
      cv::warpPerspective(srcRgb, *rectifiedRgb, transform,
                          cv::Size(candidate.dstWidth, candidate.dstHeight),
                          cv::INTER_CUBIC, cv::BORDER_REPLICATE);
      cv::warpPerspective(srcGray, *rectifiedGray, transform,
                          cv::Size(candidate.dstWidth, candidate.dstHeight),
                          cv::INTER_LINEAR, cv::BORDER_REPLICATE);
      return !rectifiedRgb->empty() && !rectifiedGray->empty();
    }

    cv::Mat buildQuadDebugOverlay(const cv::Mat &roiRgb,
                                  const PerspectiveSearchResult &search,
                                  const bool perspectiveApplied,
                                  const bool adaptiveUsed)
    {
      if (roiRgb.empty())
      {
        return cv::Mat();
      }

      cv::Mat overlay = roiRgb.clone();
      const int textThickness = std::max(1, overlay.rows / 220);
      const double textScale = std::max(0.45, overlay.rows / 520.0);

      if (search.hasCandidate && search.best.quad.size() == 4)
      {
        std::vector<cv::Point> polygon;
        polygon.reserve(4);
        for (const cv::Point2f &pt : search.best.quad)
        {
          polygon.emplace_back(cvRound(pt.x), cvRound(pt.y));
        }

        cv::polylines(overlay, polygon, true, cv::Scalar(0, 255, 255),
                      std::max(2, textThickness), cv::LINE_AA);

        for (int i = 0; i < 4; ++i)
        {
          const cv::Point corner = polygon[i];
          cv::circle(overlay, corner, std::max(3, textThickness + 1),
                     cv::Scalar(255, 120, 0), cv::FILLED, cv::LINE_AA);
          cv::putText(overlay, cv::format("%d(%d,%d)", i, corner.x, corner.y),
                      corner + cv::Point(5, -5), cv::FONT_HERSHEY_SIMPLEX,
                      textScale * 0.8, cv::Scalar(255, 255, 255), textThickness,
                      cv::LINE_AA);
        }
      }

      const char *sourceLabel = search.hasCandidate
                                    ? (search.best.fromMinAreaRect ? "minRect"
                                                                   : "approx4")
                                    : "none";
      const cv::String line1 = perspectiveApplied
                                   ? cv::format("perspective: success (%s)", sourceLabel)
                                   : "perspective: fallback(original)";
      const cv::String line2 =
          search.hasCandidate
              ? cv::format("score=%.1f dst=%dx%d", search.best.score,
                           search.best.dstWidth, search.best.dstHeight)
              : "score=n/a dst=n/a";
      const cv::String line3 = cv::format("upscale=postwarp adaptive=%s",
                                          adaptiveUsed ? "on" : "off");

      cv::putText(overlay, line1, cv::Point(8, 24), cv::FONT_HERSHEY_SIMPLEX,
                  textScale, cv::Scalar(0, 0, 0), textThickness + 2, cv::LINE_AA);
      cv::putText(overlay, line1, cv::Point(8, 24), cv::FONT_HERSHEY_SIMPLEX,
                  textScale, cv::Scalar(255, 255, 255), textThickness, cv::LINE_AA);
      cv::putText(overlay, line2, cv::Point(8, 52), cv::FONT_HERSHEY_SIMPLEX,
                  textScale * 0.95, cv::Scalar(0, 0, 0), textThickness + 2,
                  cv::LINE_AA);
      cv::putText(overlay, line2, cv::Point(8, 52), cv::FONT_HERSHEY_SIMPLEX,
                  textScale * 0.95, cv::Scalar(255, 255, 255), textThickness,
                  cv::LINE_AA);
      cv::putText(overlay, line3, cv::Point(8, 80), cv::FONT_HERSHEY_SIMPLEX,
                  textScale * 0.95, cv::Scalar(0, 0, 0), textThickness + 2,
                  cv::LINE_AA);
      cv::putText(overlay, line3, cv::Point(8, 80), cv::FONT_HERSHEY_SIMPLEX,
                  textScale * 0.95, cv::Scalar(255, 255, 255), textThickness,
                  cv::LINE_AA);

      return overlay;
    }

  } // namespace

  bool preprocessPlateRoi(const cv::Mat &roiRgb, PreprocessOutput *out)
  {
    if (!out)
    {
      return false;
    }

    *out = PreprocessOutput();
    if (roiRgb.empty())
    {
      return false;
    }

    constexpr bool upscaleAfterWarp = true;

    cv::Mat perspectiveRgbInput = roiRgb;

    cv::Mat perspectiveGrayInput;
    cv::cvtColor(perspectiveRgbInput, perspectiveGrayInput, cv::COLOR_RGB2GRAY);

    const PerspectiveSearchResult perspective =
        findPerspectiveCandidate(perspectiveGrayInput);

    cv::Mat rectifiedRgb = perspectiveRgbInput;
    cv::Mat rectifiedGray = perspectiveGrayInput;
    bool perspectiveApplied = false;
    if (perspective.accepted)
    {
      cv::Mat warpedRgb;
      cv::Mat warpedGray;
      if (applyPerspectiveCandidate(perspectiveRgbInput, perspectiveGrayInput,
                                    perspective.best, &warpedRgb, &warpedGray))
      {
        rectifiedRgb = warpedRgb;
        rectifiedGray = warpedGray;
        perspectiveApplied = true;
      }
    }

    cv::Mat ocrRgb;
    cv::Mat ocrGray;
    cv::resize(rectifiedRgb, ocrRgb, cv::Size(), kOcrUpscaleFactor,
               kOcrUpscaleFactor, cv::INTER_CUBIC);
    cv::resize(rectifiedGray, ocrGray, cv::Size(), kOcrUpscaleFactor,
               kOcrUpscaleFactor, cv::INTER_CUBIC);

    cv::Mat binary;
    cv::threshold(ocrGray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    cv::Mat binaryInv;
    cv::bitwise_not(binary, binaryInv);

    cv::Mat adaptiveBinary;
    bool adaptiveUsed = false;
    if (shouldUseAdaptiveThreshold(binary))
    {
      int blockSize = kAdaptiveBlockSize;
      const int minSide = std::min(ocrGray.rows, ocrGray.cols);
      if (minSide < 3)
      {
        blockSize = 0;
      }
      else
      {
        if (blockSize >= minSide)
        {
          blockSize = (minSide % 2 == 0) ? (minSide - 1) : minSide;
        }
        if (blockSize % 2 == 0)
        {
          --blockSize;
        }
        blockSize = std::max(3, blockSize);
      }

      if (blockSize >= 3)
      {
        cv::adaptiveThreshold(ocrGray, adaptiveBinary, 255,
                              cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY,
                              blockSize, kAdaptiveC);
        adaptiveUsed = !adaptiveBinary.empty();
      }
    }

    out->roiDebugRgb = perspectiveRgbInput;
    out->ocrGray = ocrGray;
    out->ocrRgb = ocrRgb;
    out->binary = binary;
    out->binaryInv = binaryInv;
    out->adaptiveBinary = adaptiveBinary;
    out->adaptiveUsed = adaptiveUsed;
    out->upscaleAfterWarp = upscaleAfterWarp;
    out->perspectiveApplied = perspectiveApplied;
    out->quadDebug = buildQuadDebugOverlay(perspectiveRgbInput, perspective,
                                           perspectiveApplied, adaptiveUsed);

    return !out->binary.empty() && !out->binaryInv.empty();
  }

} // namespace ocr::preprocess
