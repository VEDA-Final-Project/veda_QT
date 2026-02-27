#include "videoframerenderer.h"
#include "config/config.h"
#include <QPainter>
#include <QRegion>
#include <QVector>
#include <QtGlobal>
#include <opencv2/imgproc.hpp>

namespace {
QRegion roiRegionOnFrame(const QRect &frameRect,
                         const QList<QPolygon> &roiPolygons) {
  QRegion region;
  const QRegion frameRegion(frameRect);
  for (const QPolygon &polygon : roiPolygons) {
    if (polygon.size() < 3) {
      continue;
    }
    region = region.united(
        QRegion(polygon, Qt::WindingFill).intersected(frameRegion));
  }
  return region;
}

QList<QPolygon> scaleRoiPolygons(const QList<QPolygon> &roiPolygons,
                                 const QSize &sourceSize,
                                 const QSize &targetSize) {
  QList<QPolygon> scaledPolygons;
  if (sourceSize.isEmpty() || targetSize.isEmpty()) {
    return scaledPolygons;
  }

  scaledPolygons.reserve(roiPolygons.size());
  const double sx =
      static_cast<double>(targetSize.width()) / sourceSize.width();
  const double sy =
      static_cast<double>(targetSize.height()) / sourceSize.height();
  const int maxX = qMax(0, targetSize.width() - 1);
  const int maxY = qMax(0, targetSize.height() - 1);

  for (const QPolygon &polygon : roiPolygons) {
    if (polygon.size() < 3) {
      continue;
    }

    QPolygon scaledPolygon;
    scaledPolygon.reserve(polygon.size());
    for (const QPoint &pt : polygon) {
      const int x = qBound(0, static_cast<int>(pt.x() * sx), maxX);
      const int y = qBound(0, static_cast<int>(pt.y() * sy), maxY);
      scaledPolygon << QPoint(x, y);
    }

    if (scaledPolygon.size() >= 3 && !scaledPolygon.boundingRect().isEmpty()) {
      scaledPolygons.append(scaledPolygon);
    }
  }

  return scaledPolygons;
}

// ROI 영역의 픽셀 면적 계산
double regionPixelArea(const QRegion &region) {
  double area = 0.0;
  for (const QRect &r : region) {
    area += static_cast<double>(r.width()) * r.height();
  }
  return area;
}
} // namespace

QImage VideoFrameRenderer::compose(const QImage &frame, const QSize &targetSize,
                                   const QList<ObjectInfo> &objects,
                                   const QList<QPolygon> &roiPolygons,
                                   const QStringList &roiLabels,
                                   bool roiEnabled, bool showFps,
                                   int currentFps,
                                   QList<OcrRequest> *ocrRequests) const {
  if (targetSize.isEmpty()) {
    return frame;
  }

  // 1. OpenCV INTER_AREA 기반 빠른 고화질 축소
  // Qt::SmoothTransformation보다 2~3배 빠르면서 화질 차이가 거의 없습니다.
  // frame(QImage)을 cv::Mat으로 래핑(Zero-Copy)한 뒤 OpenCV로 축소합니다.
  const QImage rgbFrame = frame.format() == QImage::Format_RGB888
                              ? frame
                              : frame.convertToFormat(QImage::Format_RGB888);
  const cv::Mat srcMat(rgbFrame.height(), rgbFrame.width(), CV_8UC3,
                       const_cast<uchar *>(rgbFrame.bits()),
                       static_cast<size_t>(rgbFrame.bytesPerLine()));

  // KeepAspectRatio 계산
  const QSize scaledSize = frame.size().scaled(targetSize, Qt::KeepAspectRatio);
  cv::Mat dstMat;
  cv::resize(srcMat, dstMat, cv::Size(scaledSize.width(), scaledSize.height()),
             0, 0, cv::INTER_AREA);

  // cv::Mat → QImage 래핑 (dstMat의 데이터를 복사하여 독립적인 QImage 생성)
  QImage scaledFrame(dstMat.data, dstMat.cols, dstMat.rows,
                     static_cast<int>(dstMat.step), QImage::Format_RGB888);
  scaledFrame = scaledFrame.copy(); // dstMat 스코프 종료에 대비한 안전한 복사

  QPainter painter(&scaledFrame);

  QPen pen(Qt::green, 3);
  painter.setPen(pen);

  QFont font = painter.font();
  font.setPointSize(14); // May need dynamic adjustment based on targetSize, but
                         // 14 is a good start
  font.setBold(true);
  painter.setFont(font);

  const QList<QPolygon> scaledRoiPolygons =
      scaleRoiPolygons(roiPolygons, frame.size(), scaledFrame.size());

  const auto &cfg = Config::instance();
  const double sourceHeight = static_cast<double>(cfg.sourceHeight());
  const double effectiveWidth = static_cast<double>(cfg.effectiveWidth());
  const double cropOffsetX = static_cast<double>(cfg.cropOffsetX());
  const QRegion roiRegion =
      roiRegionOnFrame(scaledFrame.rect(), scaledRoiPolygons);
  const bool hasActiveRoi = roiEnabled && !roiRegion.isEmpty();

  struct RenderCandidate {
    ObjectInfo obj;
    QRect scaledRect; // Used for UI drawing
    QRect srcRect;    // Used for OCR Cropping (Full Res)
    bool intersectsRoi = false;
  };
  QVector<RenderCandidate> candidates;
  candidates.reserve(objects.size());
  bool hasAnyRoiMatch = false;

  for (const ObjectInfo &obj : objects) {
    const QRectF &nsRect =
        obj.rect; // Normalized-like source rect (from AI metadata)

    // Calculate Coordinates for UI Rendering (Scaled)
    const double scaledX =
        ((nsRect.x() - cropOffsetX) / effectiveWidth) * scaledFrame.width();
    const double scaledY = (nsRect.y() / sourceHeight) * scaledFrame.height();
    const double scaledW =
        (nsRect.width() / effectiveWidth) * scaledFrame.width();
    const double scaledH =
        (nsRect.height() / sourceHeight) * scaledFrame.height();

    const QRect uRect(static_cast<int>(scaledX), static_cast<int>(scaledY),
                      static_cast<int>(scaledW), static_cast<int>(scaledH));

    // Calculate Coordinates for OCR Cropping (Full 4K Res)
    const double srcX =
        ((nsRect.x() - cropOffsetX) / effectiveWidth) * frame.width();
    const double srcY = (nsRect.y() / sourceHeight) * frame.height();
    const double srcW = (nsRect.width() / effectiveWidth) * frame.width();
    const double srcH = (nsRect.height() / sourceHeight) * frame.height();

    const QRect fullSrcRect(static_cast<int>(srcX), static_cast<int>(srcY),
                            static_cast<int>(srcW), static_cast<int>(srcH));

    const bool intersects = hasActiveRoi && roiRegion.intersects(uRect);
    hasAnyRoiMatch = hasAnyRoiMatch || intersects;
    candidates.push_back(RenderCandidate{obj, uRect, fullSrcRect, intersects});
  }

  // If ROI exists but no object intersects, fallback to full rendering/OCR.
  const bool shouldFilterByRoi = hasActiveRoi && hasAnyRoiMatch;

  if (hasActiveRoi) {
    for (int i = 0; i < scaledRoiPolygons.size(); ++i) {
      const QPolygon &polygon = scaledRoiPolygons[i];
      if (polygon.size() < 3) {
        continue;
      }

      // ROI별 차량 점유 여부 판단 (50% 이상 점유 시 주차로 인식)
      const QRegion singleRoiRegion(polygon, Qt::WindingFill);
      const double roiArea = regionPixelArea(singleRoiRegion);
      bool occupied = false;
      for (const RenderCandidate &c : candidates) {
        if (!c.obj.type.startsWith("Vehic"))
          continue;
        const QRegion intersection =
            singleRoiRegion.intersected(QRegion(c.scaledRect));
        const double interArea = regionPixelArea(intersection);
        if (roiArea > 0 && (interArea / roiArea) >= 0.5) {
          occupied = true;
          break;
        }
      }

      // 점유 상태에 따라 색상 변경
      if (occupied) {
        painter.setPen(QPen(QColor(0, 200, 0), 3, Qt::SolidLine));
      } else {
        painter.setPen(QPen(Qt::red, 2, Qt::DashLine));
      }

      painter.drawPolygon(polygon);

      // 라벨 텍스트
      const QString baseName =
          (i < roiLabels.size() && !roiLabels[i].trimmed().isEmpty())
              ? roiLabels[i].trimmed()
              : QString("ROI %1").arg(i + 1);
      const QString label =
          occupied ? QString("%1 [Parked]").arg(baseName) : baseName;

      QRect textRect = painter.fontMetrics().boundingRect(label);
      textRect.adjust(-6, -3, 6, 3);
      const QPoint anchor = polygon.boundingRect().topLeft() + QPoint(2, 2);
      textRect.moveTopLeft(anchor);

      painter.fillRect(textRect, occupied ? QColor(0, 140, 0, 200)
                                          : QColor(180, 0, 0, 180));
      painter.setPen(Qt::white);
      painter.drawText(textRect, Qt::AlignCenter, label);
    }
    painter.setPen(pen);
  }

  for (const RenderCandidate &candidate : candidates) {
    // 사용자의 요청에 따라 ROI 점유 상태와 무관하게 모든 객체(바운딩 박스)를
    // 항상 그립니다.

    const ObjectInfo &obj = candidate.obj;
    const QRect &uRect = candidate.scaledRect;
    painter.drawRect(uRect);

    QString text = QString("%1 (ID:%2)").arg(obj.type).arg(obj.id);
    if (!obj.extraInfo.isEmpty()) {
      text += QString(" [%1]").arg(obj.extraInfo);
    }

    if (obj.type == "LicensePlate" && ocrRequests != nullptr) {
      // 2. OCR Decoupling: Extract from original FULL RES frame, not
      // scaledFrame
      // 2-1. 바운딩 박스 여백(Padding) 추가: 가로 약 3%, 세로 약 7% (기존의
      // 1/3로 축소)
      int padX = static_cast<int>(candidate.srcRect.width() * 0.033);
      int padY = static_cast<int>(candidate.srcRect.height() * 0.066);
      QRect paddedRect = candidate.srcRect.adjusted(-padX, -padY, padX, padY);

      const QRect safeRect = paddedRect.intersected(frame.rect());
      if (!safeRect.isEmpty()) {
        ocrRequests->append(OcrRequest{obj.id, frame.copy(safeRect)});
      }
    }

    QRect textRect = painter.fontMetrics().boundingRect(text);
    textRect.moveTopLeft(uRect.topLeft() - QPoint(0, textRect.height() + 5));

    painter.fillRect(textRect, Qt::black);
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
    painter.setPen(pen);
  }

  if (objects.isEmpty()) {
    painter.setPen(Qt::yellow);
    painter.drawText(10, 30, "Waiting for AI Data...");
  }

  if (showFps) {
    QString fpsText = QString("FPS: %1").arg(currentFps);
    QFont fpsFont = painter.font();
    fpsFont.setPointSize(16);
    fpsFont.setBold(true);
    painter.setFont(fpsFont);

    const int margin = 10;
    QRect textRect = painter.fontMetrics().boundingRect(fpsText);
    textRect.moveTo(scaledFrame.width() - textRect.width() - margin, margin);
    textRect.adjust(-4, -2, 4, 2);

    painter.fillRect(textRect, QColor(0, 0, 0, 150));
    painter.setPen(QColor(0, 255, 0)); // Green text
    painter.drawText(textRect, Qt::AlignCenter, fpsText);
  }

  painter.end();
  return scaledFrame;
}
