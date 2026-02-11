#include "videoframerenderer.h"
#include "core/config.h"
#include <QPainter>
#include <QRegion>
#include <QVector>

namespace
{
QRegion roiRegionOnFrame(const QRect &frameRect, const QList<QPolygon> &roiPolygons)
{
  QRegion region;
  const QRegion frameRegion(frameRect);
  for (const QPolygon &polygon : roiPolygons)
  {
    if (polygon.size() < 3)
    {
      continue;
    }
    region = region.united(QRegion(polygon, Qt::WindingFill).intersected(frameRegion));
  }
  return region;
}
} // namespace

QImage VideoFrameRenderer::compose(const QImage &frame, const QList<ObjectInfo> &objects,
                                   const QList<QPolygon> &roiPolygons,
                                   const QStringList &roiLabels, bool roiEnabled,
                                   QList<OcrRequest> *ocrRequests) const
{
  QImage keyFrame = frame;
  QPainter painter(&keyFrame);

  QPen pen(Qt::green, 3);
  painter.setPen(pen);

  QFont font = painter.font();
  font.setPointSize(14);
  font.setBold(true);
  painter.setFont(font);

  const auto &cfg = Config::instance();
  const double sourceHeight = static_cast<double>(cfg.sourceHeight());
  const double effectiveWidth = static_cast<double>(cfg.effectiveWidth());
  const double cropOffsetX = static_cast<double>(cfg.cropOffsetX());
  const QRegion roiRegion = roiRegionOnFrame(keyFrame.rect(), roiPolygons);
  const bool hasActiveRoi = roiEnabled && !roiRegion.isEmpty();

  struct RenderCandidate
  {
    ObjectInfo obj;
    QRect rect;
    bool intersectsRoi = false;
  };
  QVector<RenderCandidate> candidates;
  candidates.reserve(objects.size());
  bool hasAnyRoiMatch = false;

  for (const ObjectInfo &obj : objects)
  {
    const QRectF &srcRect = obj.rect;
    const double x = ((srcRect.x() - cropOffsetX) / effectiveWidth) * keyFrame.width();
    const double y = (srcRect.y() / sourceHeight) * keyFrame.height();
    const double w = (srcRect.width() / effectiveWidth) * keyFrame.width();
    const double h = (srcRect.height() / sourceHeight) * keyFrame.height();

    const QRect rect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
                     static_cast<int>(h));
    const bool intersects = hasActiveRoi && roiRegion.intersects(rect);
    hasAnyRoiMatch = hasAnyRoiMatch || intersects;
    candidates.push_back(RenderCandidate{obj, rect, intersects});
  }

  // If ROI exists but no object intersects, fallback to full rendering/OCR.
  const bool shouldFilterByRoi = hasActiveRoi && hasAnyRoiMatch;

  if (hasActiveRoi)
  {
    painter.setPen(QPen(Qt::red, 2, Qt::DashLine));
    for (int i = 0; i < roiPolygons.size(); ++i)
    {
      const QPolygon &polygon = roiPolygons[i];
      if (polygon.size() >= 3)
      {
        painter.drawPolygon(polygon);
        const QString label =
            (i < roiLabels.size() && !roiLabels[i].trimmed().isEmpty())
                ? roiLabels[i].trimmed()
                : QString("ROI %1").arg(i + 1);
        QRect textRect = painter.fontMetrics().boundingRect(label);
        textRect.adjust(-6, -3, 6, 3);
        const QPoint anchor = polygon.boundingRect().topLeft() + QPoint(2, 2);
        textRect.moveTopLeft(anchor);
        painter.fillRect(textRect, QColor(180, 0, 0, 180));
        painter.setPen(Qt::white);
        painter.drawText(textRect, Qt::AlignCenter, label);
        painter.setPen(QPen(Qt::red, 2, Qt::DashLine));
      }
    }
    painter.setPen(pen);
  }

  for (const RenderCandidate &candidate : candidates)
  {
    if (shouldFilterByRoi && !candidate.intersectsRoi)
    {
      continue;
    }

    const ObjectInfo &obj = candidate.obj;
    const QRect &rect = candidate.rect;
    painter.drawRect(rect);

    QString text = QString("%1 (ID:%2)").arg(obj.type).arg(obj.id);
    if (!obj.extraInfo.isEmpty())
    {
      text += QString(" [%1]").arg(obj.extraInfo);
    }

    if (obj.type == "LicensePlate" && ocrRequests != nullptr)
    {
      const QRect safeRect = rect.intersected(keyFrame.rect());
      if (!safeRect.isEmpty())
      {
        ocrRequests->append(OcrRequest{obj.id, keyFrame.copy(safeRect)});
      }
    }

    QRect textRect = painter.fontMetrics().boundingRect(text);
    textRect.moveTopLeft(rect.topLeft() - QPoint(0, textRect.height() + 5));

    painter.fillRect(textRect, Qt::black);
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
    painter.setPen(pen);
  }

  if (objects.isEmpty())
  {
    painter.setPen(Qt::yellow);
    painter.drawText(10, 30, "Waiting for AI Data...");
  }

  painter.end();
  return keyFrame;
}
