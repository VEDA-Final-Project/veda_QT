#include "roiinteractionstate.h"
#include <QDebug>
#include <QtGlobal>

void RoiInteractionState::setUserRoi(const QRect &roi)
{
  m_userRoiPolygons.clear();
  addUserRoi(roi);
}

void RoiInteractionState::addUserRoi(const QRect &roi)
{
  const QRect normalized = roi.normalized();
  if (normalized.isEmpty())
  {
    return;
  }

  QPolygon polygon;
  polygon << normalized.topLeft() << normalized.topRight() << normalized.bottomRight()
          << normalized.bottomLeft();
  addUserRoiPolygon(polygon);
}

void RoiInteractionState::addUserRoiPolygon(const QPolygon &polygon)
{
  if (polygon.size() < 3)
  {
    return;
  }

  const QRect bounds = polygon.boundingRect().normalized();
  if (bounds.isEmpty())
  {
    return;
  }

  m_userRoiPolygons.append(polygon);
  m_roiEnabled = !m_userRoiPolygons.isEmpty();
  qDebug() << "[ROI][VideoWidget] polygon add: points=" << polygon.size()
           << "bounds=" << bounds << "count=" << m_userRoiPolygons.size();
}

void RoiInteractionState::startDrawing()
{
  m_drawingMode = true;
  m_drawingPolygonPoints.clear();
  m_hasHoverPoint = false;
}

RoiFinishResult RoiInteractionState::finishDrawing()
{
  RoiFinishResult result;

  if (!m_drawingMode)
  {
    return result;
  }

  if (m_drawingPolygonPoints.size() >= 3)
  {
    QPolygon polygon(m_drawingPolygonPoints);
    addUserRoiPolygon(polygon);
    result.completed = true;
    result.polygon = polygon;
    result.boundingRect = polygon.boundingRect();
  }

  m_drawingMode = false;
  m_drawingPolygonPoints.clear();
  m_hasHoverPoint = false;
  return result;
}

bool RoiInteractionState::handleMousePress(QMouseEvent *event, const QSize &widgetSize,
                                           const QSize &frameSize)
{
  if (!m_drawingMode || frameSize.isEmpty() || event->button() != Qt::LeftButton)
  {
    return false;
  }

  const QRect pixRect = displayedPixmapRect(widgetSize, frameSize);
  const QPoint widgetPoint = event->position().toPoint();
  if (!pixRect.contains(widgetPoint))
  {
    return false;
  }

  const QPoint framePoint = mapWidgetToFrame(widgetPoint, widgetSize, frameSize);
  m_drawingPolygonPoints.append(framePoint);
  m_hoverFramePoint = framePoint;
  m_hasHoverPoint = true;
  return true;
}

bool RoiInteractionState::handleMouseMove(QMouseEvent *event, const QSize &widgetSize,
                                          const QSize &frameSize)
{
  if (!m_drawingMode || frameSize.isEmpty())
  {
    return false;
  }

  const QRect pixRect = displayedPixmapRect(widgetSize, frameSize);
  const QPoint widgetPoint = event->position().toPoint();
  if (pixRect.contains(widgetPoint))
  {
    m_hoverFramePoint = mapWidgetToFrame(widgetPoint, widgetSize, frameSize);
    m_hasHoverPoint = true;
  }
  else
  {
    m_hasHoverPoint = false;
  }
  return true;
}

void RoiInteractionState::paintDrawingOverlay(QWidget *widget, const QSize &frameSize) const
{
  if (!m_drawingMode || frameSize.isEmpty())
  {
    return;
  }

  const QRect pixRect = displayedPixmapRect(widget->size(), frameSize);
  if (pixRect.isEmpty())
  {
    return;
  }

  QPainter painter(widget);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(Qt::red, 2, Qt::DashLine));

  const double sx = static_cast<double>(pixRect.width()) / frameSize.width();
  const double sy = static_cast<double>(pixRect.height()) / frameSize.height();

  auto toWidget = [&](const QPoint &framePoint) -> QPoint {
    return QPoint(pixRect.x() + static_cast<int>(framePoint.x() * sx),
                  pixRect.y() + static_cast<int>(framePoint.y() * sy));
  };

  QVector<QPoint> widgetPoints;
  widgetPoints.reserve(m_drawingPolygonPoints.size());
  for (const QPoint &pt : m_drawingPolygonPoints)
  {
    widgetPoints.append(toWidget(pt));
  }

  for (int i = 1; i < widgetPoints.size(); ++i)
  {
    painter.drawLine(widgetPoints[i - 1], widgetPoints[i]);
  }

  if (!widgetPoints.isEmpty() && m_hasHoverPoint)
  {
    painter.drawLine(widgetPoints.last(), toWidget(m_hoverFramePoint));
  }

  if (widgetPoints.size() >= 3)
  {
    const QPoint lastPoint = (m_hasHoverPoint ? toWidget(m_hoverFramePoint) : widgetPoints.last());
    painter.drawLine(lastPoint, widgetPoints.first());
  }

  painter.setPen(QPen(Qt::yellow, 2));
  painter.setBrush(Qt::yellow);
  for (const QPoint &pt : widgetPoints)
  {
    painter.drawEllipse(pt, 3, 3);
  }
}

const QList<QPolygon> &RoiInteractionState::roiPolygons() const
{
  return m_userRoiPolygons;
}

bool RoiInteractionState::roiEnabled() const
{
  return m_roiEnabled;
}

QRect RoiInteractionState::displayedPixmapRect(const QSize &widgetSize, const QSize &frameSize) const
{
  if (frameSize.isEmpty())
  {
    return QRect();
  }
  const QSize scaledSize = frameSize.scaled(widgetSize, Qt::KeepAspectRatio);
  const int x = (widgetSize.width() - scaledSize.width()) / 2;
  const int y = (widgetSize.height() - scaledSize.height()) / 2;
  return QRect(x, y, scaledSize.width(), scaledSize.height());
}

QPoint RoiInteractionState::mapWidgetToFrame(const QPoint &widgetPoint, const QSize &widgetSize,
                                             const QSize &frameSize) const
{
  if (frameSize.isEmpty())
  {
    return QPoint();
  }
  const QRect pixRect = displayedPixmapRect(widgetSize, frameSize);
  if (pixRect.isEmpty())
  {
    return QPoint();
  }

  const int clampedX = qBound(pixRect.left(), widgetPoint.x(), pixRect.right());
  const int clampedY = qBound(pixRect.top(), widgetPoint.y(), pixRect.bottom());
  const double nx = static_cast<double>(clampedX - pixRect.left()) / pixRect.width();
  const double ny = static_cast<double>(clampedY - pixRect.top()) / pixRect.height();

  const int frameX = qBound(0, static_cast<int>(nx * frameSize.width()), frameSize.width() - 1);
  const int frameY = qBound(0, static_cast<int>(ny * frameSize.height()), frameSize.height() - 1);
  return QPoint(frameX, frameY);
}
