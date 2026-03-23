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
  const double sourceWidth = static_cast<double>(cfg.sourceWidth());
  const double sourceHeight = static_cast<double>(cfg.sourceHeight());
  const QRectF &nsRect = obj.rect;

  const double srcX = (nsRect.x() / sourceWidth) * frame.width();
  const double srcY = (nsRect.y() / sourceHeight) * frame.height();
  const double srcW = (nsRect.width() / sourceWidth) * frame.width();
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

QImage VideoFrameRenderer::compose(
    const QImage &sourceFrame, const QImage &scaledBaseFrame,
    const QList<ObjectInfo> &objects, const QList<QPolygon> &roiPolygons,
    const QStringList &roiLabels, const QSet<int> &occupiedRoiIndices,
    bool roiEnabled, bool showFps, int currentFps, const QString &profileName,
    double zoom, double panX, double panY) const {

  if (scaledBaseFrame.isNull() || sourceFrame.isNull()) {
    return scaledBaseFrame;
  }

  QImage renderTarget;
  QRectF zoomWindow; // Normalized (0.0 - 1.0) window on sourceFrame
  const QSize outputSize = scaledBaseFrame.size();

  if (zoom > 1.001) {
    const double w = 1.0 / zoom;
    const double h = 1.0 / zoom;
    // panX, panY is the center of the zoom window (normalized)
    double x = panX - w / 2.0;
    double y = panY - h / 2.0;
    x = qBound(0.0, x, 1.0 - w);
    y = qBound(0.0, y, 1.0 - h);
    zoomWindow = QRectF(x, y, w, h);

    const int srcX = static_cast<int>(zoomWindow.x() * sourceFrame.width());
    const int srcY = static_cast<int>(zoomWindow.y() * sourceFrame.height());
    const int srcW = static_cast<int>(zoomWindow.width() * sourceFrame.width());
    const int srcH =
        static_cast<int>(zoomWindow.height() * sourceFrame.height());

    const QImage crop = sourceFrame.copy(srcX, srcY, srcW, srcH);
    renderTarget =
        crop.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
  } else {
    zoomWindow = QRectF(0, 0, 1.0, 1.0);
    renderTarget = scaledBaseFrame.copy();
  }

  if (renderTarget.format() != QImage::Format_RGB888 &&
      renderTarget.format() != QImage::Format_BGR888 &&
      renderTarget.format() != QImage::Format_RGB32 &&
      renderTarget.format() != QImage::Format_ARGB32_Premultiplied) {
    renderTarget = renderTarget.convertToFormat(QImage::Format_RGB888);
  }

  QPainter painter(&renderTarget);
  QPen pen(Qt::green, 3);
  painter.setPen(pen);

  const auto &cfg = Config::instance();
  const double sourceWidth = static_cast<double>(cfg.sourceWidth());
  const double sourceHeight = static_cast<double>(cfg.sourceHeight());

  QFont font = painter.font();
  font.setPointSize(14);
  font.setBold(true);
  painter.setFont(font);

  // ROI / Object coordinates mapping function (Source -> RenderTarget)
  auto mapPoint = [&](const QPointF &sourcePt) -> QPoint {
    // 1. Convert sourcePt (pixel on sourceFrame) to normalized (0.0-1.0)
    double normX = sourcePt.x() / sourceFrame.width();
    double normY = sourcePt.y() / sourceFrame.height();

    // 2. Map normalized source coordinates to zoomed normalized coordinates
    double zoomX = (normX - zoomWindow.x()) / zoomWindow.width();
    double zoomY = (normY - zoomWindow.y()) / zoomWindow.height();

    // 3. Map to renderTarget pixels
    return QPoint(static_cast<int>(zoomX * outputSize.width()),
                  static_cast<int>(zoomY * outputSize.height()));
  };

  // Draw ROIs
  if (roiEnabled) {
    for (int i = 0; i < roiPolygons.size(); ++i) {
      const QPolygon &polygon = roiPolygons[i];
      if (polygon.size() < 3)
        continue;

      QPolygon mappedPoly;
      mappedPoly.reserve(polygon.size());
      for (const QPoint &pt : polygon) {
        mappedPoly << mapPoint(QPointF(pt));
      }

      if (mappedPoly.boundingRect().isEmpty())
        continue;

      const bool occupied = occupiedRoiIndices.contains(i);
      if (occupied) {
        painter.setPen(QPen(QColor(255, 59, 48), 2, Qt::SolidLine));
      } else {
        painter.setPen(QPen(QColor(0, 229, 255), 2, Qt::SolidLine));
      }
      painter.drawPolygon(mappedPoly);

      const QString label =
          (i < roiLabels.size() && !roiLabels[i].trimmed().isEmpty())
              ? (occupied ? QString("%1 [Parked]").arg(roiLabels[i].trimmed())
                          : roiLabels[i].trimmed())
              : (occupied ? QString("Zone %1 [Parked]").arg(i + 1)
                          : QString("Zone %1").arg(i + 1));

      QRect textRect = painter.fontMetrics().boundingRect(label);
      textRect.adjust(-6, -3, 6, 3);
      const QPoint anchor = mappedPoly.boundingRect().topLeft() + QPoint(2, 2);
      textRect.moveTopLeft(anchor);

      if (occupied) {
        painter.fillRect(textRect, QColor(255, 59, 48, 210));
        painter.setPen(Qt::white);
      } else {
        painter.fillRect(textRect, QColor(0, 229, 255, 200));
        painter.setPen(QColor(27, 31, 42));
      }
      painter.drawText(textRect, Qt::AlignCenter, label);
    }
  }

  // Draw Objects
  painter.setPen(pen);
  for (const ObjectInfo &obj : objects) {
    const QRectF &nsRect = obj.rect;
    // nsRect is usually based on "sourceWidth/sourceHeight" config, NOT
    // necessarily sourceFrame size (sometimes).
    // Let's use normalization to be safe.
    double normX = nsRect.x() / sourceWidth;
    double normY = nsRect.y() / sourceHeight;
    double normW = nsRect.width() / sourceWidth;
    double normH = nsRect.height() / sourceHeight;

    // Convert to zoomed normalized coordinates
    double zoomX = (normX - zoomWindow.x()) / zoomWindow.width();
    double zoomY = (normY - zoomWindow.y()) / zoomWindow.height();
    double zoomW = normW / zoomWindow.width();
    double zoomH = normH / zoomWindow.height();

    const QRect uRect(static_cast<int>(zoomX * outputSize.width()),
                      static_cast<int>(zoomY * outputSize.height()),
                      static_cast<int>(zoomW * outputSize.width()),
                      static_cast<int>(zoomH * outputSize.height()));

    // Skip if completely outside zoomed area
    if (!uRect.intersects(QRect(0, 0, outputSize.width(), outputSize.height())))
      continue;

    painter.drawRect(uRect);

    QString lowerType = obj.type.toLower();
    bool isVehicle = (lowerType == "vehicle" || lowerType == "car" ||
                      lowerType == "vehical" || lowerType == "truck" ||
                      lowerType == "bus");

    QString text;
    if (isVehicle) {
      const QString displayId = obj.reidId.isEmpty() ? "V---" : obj.reidId;
      text = QString("[%1] Vehicle").arg(displayId);
    } else {
      text = obj.type;
    }
    if (!obj.extraInfo.isEmpty() && obj.extraInfo != obj.plate) {
      text += QString(" (%1)").arg(obj.extraInfo);
    }

    QRect textRect = painter.fontMetrics().boundingRect(text);
    textRect.adjust(-4, -2, 4, 2);
    QPoint targetPos = uRect.topLeft() - QPoint(0, textRect.height());
    if (targetPos.y() < 0)
      targetPos.setY(uRect.top() + 2);
    if (targetPos.x() < 0)
      targetPos.setX(0);
    textRect.moveTopLeft(targetPos);

    painter.fillRect(textRect, QColor(0, 0, 0, 180));
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
    painter.setPen(pen);
  }

  if (showFps) {
    QString shortProfileName = profileName;
    if (shortProfileName.contains('/')) {
      shortProfileName = shortProfileName.split('/').first();
    }
    QString overlayText = QString("%1(%2x%3) , FPS: %4")
                              .arg(shortProfileName)
                              .arg(sourceFrame.width())
                              .arg(sourceFrame.height())
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
    const QRect boxRect(outputSize.width() - boxW - margin, margin, boxW, boxH);

    painter.fillRect(boxRect, QColor(0, 0, 0, 180));
    painter.setPen(QColor(0, 255, 0));
    painter.drawText(boxRect, Qt::AlignCenter, overlayText);
  }

  // 줌 배율 독립 표시
  if (zoom > 1.001) {
    const QString zoomText = QString("Zoom: %1x").arg(zoom, 0, 'f', 1);
    QFont zoomFont = painter.font();
    zoomFont.setPointSize(16);
    zoomFont.setBold(true);
    painter.setFont(zoomFont);

    const QFontMetrics metrics = painter.fontMetrics();
    QRect textRect = metrics.boundingRect(zoomText);
    const int paddingX = 12;
    const int paddingY = 6;
    const int boxW = textRect.width() + (paddingX * 2);
    const int boxH = textRect.height() + (paddingY * 2);
    const int margin = 10;

    // 만약 FPS 정보가 표시 중이면 그 아래에, 아니면 우측 상단에 표시
    int boxY = margin;
    if (showFps) {
      boxY += 40; // 대략적인 FPS 박스 높이만큼 아래로
    }
    const QRect boxRect(outputSize.width() - boxW - margin, boxY, boxW, boxH);

    painter.fillRect(boxRect, QColor(0, 0, 0, 180));
    painter.setPen(QColor(0, 255, 0));
    painter.drawText(boxRect, Qt::AlignCenter, zoomText);
  }

  painter.end();
  return renderTarget;
}
