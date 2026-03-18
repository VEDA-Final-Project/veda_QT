#include "videoframerenderer.h"
#include "config/config.h"
#include <QPainter>
#include <QRegion>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <opencv2/imgproc.hpp>

namespace {
QRect sourceRectForObject(const QImage &frame, const ObjectInfo &obj) {
  const auto &cfg = Config::instance();
  const double sourceHeight = static_cast<double>(cfg.sourceHeight());
  const double effectiveWidth = static_cast<double>(cfg.effectiveWidth());
  const double cropOffsetX = static_cast<double>(cfg.cropOffsetX());
  const QRectF &nsRect = obj.rect;

  const double srcX =
      ((nsRect.x() - cropOffsetX) / effectiveWidth) * frame.width();
  const double srcY = (nsRect.y() / sourceHeight) * frame.height();
  const double srcW = (nsRect.width() / effectiveWidth) * frame.width();
  const double srcH = (nsRect.height() / sourceHeight) * frame.height();

  return QRect(static_cast<int>(srcX), static_cast<int>(srcY),
               static_cast<int>(srcW), static_cast<int>(srcH));
}

bool isVehicleMetadataType(const QString &type) {
  return type == QStringLiteral("Vehical") ||
         type == QStringLiteral("Vehicle") ||
         type == QStringLiteral("Car") || type == QStringLiteral("Truck") ||
         type == QStringLiteral("Bus") ||
         type == QStringLiteral("Motorcycle");
}

int matchedVehicleObjectId(const QImage &frame, const QList<ObjectInfo> &objects,
                           const ObjectInfo &plateObj) {
  const QRect plateRect = sourceRectForObject(frame, plateObj);
  if (plateRect.isEmpty()) {
    return -1;
  }

  int bestVehicleId = -1;
  int bestOverlapArea = -1;
  for (const ObjectInfo &candidate : objects) {
    if (!isVehicleMetadataType(candidate.type)) {
      continue;
    }

    const QRect vehicleRect = sourceRectForObject(frame, candidate);
    if (vehicleRect.isEmpty()) {
      continue;
    }

    // 번호판은 보통 차량 하단에 위치하므로, 차량 바운딩 박스의 하단 절반 영역과만 겹침도를 계산합니다.
    // 이는 차량이 줄지어 서 있을 때 뒤차의 번호판이 앞차의 상단 영역과 겹쳐 오인식되는 것을 방지합니다.
    QRect lowerHalf = vehicleRect;
    lowerHalf.setTop(vehicleRect.top() + vehicleRect.height() / 2);

    const QRect overlap = lowerHalf.intersected(plateRect);
    const int overlapArea = overlap.width() * overlap.height();
    if (overlapArea <= 0) {
      continue;
    }

    if (overlapArea > bestOverlapArea) {
      bestOverlapArea = overlapArea;
      bestVehicleId = candidate.id;
    }
  }

  return bestVehicleId;
}

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

void VideoFrameRenderer::collectOcrRequests(
    const QImage &frame, const QList<ObjectInfo> &objects,
    QList<OcrRequest> *ocrRequests) const {
  if (!ocrRequests || frame.isNull()) {
    return;
  }

  for (const ObjectInfo &obj : objects) {
    if (obj.type != "LicensePlate") {
      continue;
    }

    const int targetObjectId = matchedVehicleObjectId(frame, objects, obj);
    if (targetObjectId < 0) {
      continue;
    }

    const QRect srcRect = sourceRectForObject(frame, obj);
    const int padX = std::max(1, static_cast<int>(srcRect.width() * 0.015));
    const int padY = std::max(1, static_cast<int>(srcRect.height() * 0.03));
    const QRect paddedRect = srcRect.adjusted(-padX, -padY, padX, padY);
    const QRect safeRect = paddedRect.intersected(frame.rect());
    if (!safeRect.isEmpty()) {
      ocrRequests->append(OcrRequest{targetObjectId, frame.copy(safeRect)});
    }
  }
}

QImage VideoFrameRenderer::compose(const QImage &frame, const QSize &targetSize,
                                   const QList<ObjectInfo> &objects,
                                   const QList<QPolygon> &roiPolygons,
                                   const QStringList &roiLabels,
                                   const QSet<int> &occupiedRoiIndices,
                                   bool roiEnabled, bool showFps,
                                   int currentFps, const QString &profileName,
                                   QList<OcrRequest> *ocrRequests) const {
  (void)ocrRequests;
  if (targetSize.isEmpty()) {
    return frame;
  }

  // 1. Qt 네이티브 기반 고속 축소 (UI 스레드 병목 및 메모리 복사 제거)
  const QSize scaledSize = frame.size().scaled(targetSize, Qt::KeepAspectRatio);
  QImage scaledFrame =
      frame.scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);

  // QPainter 엔진이 안전하게 그릴 수 있도록 포맷 보장 (대부분
  // BGR888/RGB888이므로 변환 패스됨)
  if (scaledFrame.format() != QImage::Format_RGB888 &&
      scaledFrame.format() != QImage::Format_BGR888 &&
      scaledFrame.format() != QImage::Format_RGB32 &&
      scaledFrame.format() != QImage::Format_ARGB32_Premultiplied) {
    scaledFrame = scaledFrame.convertToFormat(QImage::Format_RGB888);
  }

  QPainter painter(&scaledFrame);
  QPen pen(Qt::green, 3);
  painter.setPen(pen);

  QFont font = painter.font();
  font.setPointSize(14);
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
    QRect scaledRect;
    QRect srcRect;
    bool intersectsRoi = false;
  };
  QVector<RenderCandidate> candidates;
  candidates.reserve(objects.size());
  bool hasAnyRoiMatch = false;

  for (const ObjectInfo &obj : objects) {
    const QRectF &nsRect = obj.rect;
    const double scaledX =
        ((nsRect.x() - cropOffsetX) / effectiveWidth) * scaledFrame.width();
    const double scaledY = (nsRect.y() / sourceHeight) * scaledFrame.height();
    const double scaledW =
        (nsRect.width() / effectiveWidth) * scaledFrame.width();
    const double scaledH =
        (nsRect.height() / sourceHeight) * scaledFrame.height();

    const QRect uRect(static_cast<int>(scaledX), static_cast<int>(scaledY),
                      static_cast<int>(scaledW), static_cast<int>(scaledH));

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

  if (hasActiveRoi) {
    for (int i = 0; i < scaledRoiPolygons.size(); ++i) {
      const QPolygon &polygon = scaledRoiPolygons[i];
      if (polygon.size() < 3)
        continue;

      const bool occupied = occupiedRoiIndices.contains(i);

      if (occupied) {
        painter.setPen(QPen(QColor(255, 59, 48), 2, Qt::SolidLine));
      } else {
        painter.setPen(QPen(QColor(0, 229, 255), 2, Qt::SolidLine));
      }
      painter.drawPolygon(polygon);

      const QString baseName =
          (i < roiLabels.size() && !roiLabels[i].trimmed().isEmpty())
              ? roiLabels[i].trimmed()
              : QString("Zone %1").arg(i + 1);
      const QString label =
          occupied ? QString("%1 [Parked]").arg(baseName) : baseName;

      QRect textRect = painter.fontMetrics().boundingRect(label);
      textRect.adjust(-6, -3, 6, 3);
      const QPoint anchor = polygon.boundingRect().topLeft() + QPoint(2, 2);
      textRect.moveTopLeft(anchor);

      if (occupied) {
        painter.fillRect(textRect, QColor(255, 59, 48, 210));
        painter.setPen(Qt::white);
      } else {
        painter.fillRect(textRect, QColor(0, 229, 255, 200));
        painter.setPen(
            QColor(27, 31, 42)); // Dark Navy text for better contrast
      }
      painter.drawText(textRect, Qt::AlignCenter, label);
    }
    painter.setPen(pen);
  }

  for (const RenderCandidate &candidate : candidates) {
    const ObjectInfo &obj = candidate.obj;
    const QRect &uRect = candidate.scaledRect;
    painter.drawRect(uRect);

    const QString lowerType = obj.type.toLower();
    const bool vehicle = lowerType == QStringLiteral("vehicle") ||
                         lowerType == QStringLiteral("vehical") ||
                         lowerType == QStringLiteral("car") ||
                         lowerType == QStringLiteral("truck") ||
                         lowerType == QStringLiteral("bus");

    QString text;
    if (vehicle) {
      const QString displayId =
          obj.reidId.isEmpty() ? QStringLiteral("V---") : obj.reidId;
      text = QString("[%1] Vehicle").arg(displayId);
    } else {
      text = QString("%1 (ID:%2)").arg(obj.type).arg(obj.id);
    }
    if (!obj.extraInfo.isEmpty() && obj.extraInfo != obj.plate) {
      text += QString(" (%1)").arg(obj.extraInfo);
    }
    if (!obj.plate.isEmpty()) {
      text += QString(" [%1]").arg(obj.plate);
    }

    QRect textRect = painter.fontMetrics().boundingRect(text);
    textRect.moveTopLeft(uRect.topLeft() - QPoint(0, textRect.height() + 5));

    painter.fillRect(textRect, Qt::black);
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
    painter.setPen(pen);
  }

  if (showFps) {
    // === 우측 상단 정보 오버레이 강화 (프로파일 + 해상도 + FPS) ===
    QString shortProfileName = profileName;
    if (shortProfileName.contains('/')) {
      shortProfileName = shortProfileName.split('/').first();
    }
    const QString overlayText = QString("%1(%2x%3) , FPS: %4")
                                    .arg(shortProfileName)
                                    .arg(frame.width())
                                    .arg(frame.height())
                                    .arg(currentFps);

    QFont fpsFont = painter.font();
    fpsFont.setPointSize(14);
    fpsFont.setBold(true);
    painter.setFont(fpsFont);

    const int margin = 10;
    const QFontMetrics metrics = painter.fontMetrics();
    QRect textRect = metrics.boundingRect(overlayText);

    const int paddingX = 8;
    const int paddingY = 4;
    const int boxW = textRect.width() + (paddingX * 2);
    const int boxH = textRect.height() + (paddingY * 2);

    const QRect boxRect(scaledFrame.width() - boxW - margin, margin, boxW,
                        boxH);

    painter.fillRect(boxRect, QColor(0, 0, 0, 180));
    painter.setPen(QColor(0, 255, 0));
    painter.drawText(boxRect, Qt::AlignCenter, overlayText);
  }

  painter.end();
  return scaledFrame;
}
