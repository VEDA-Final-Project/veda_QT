#include "roiinteractionstate.h"
#include <QDebug>
#include <QtGlobal>

void RoiInteractionState::setUserRoi(const QRect &roi) {
  m_userRoiPolygons.clear();
  addUserRoi(roi);
}

void RoiInteractionState::addUserRoi(const QRect &roi) {
  const QRect normalized = roi.normalized();
  if (normalized.isEmpty()) {
    return;
  }

  QPolygon polygon;
  polygon << normalized.topLeft() << normalized.topRight()
          << normalized.bottomRight() << normalized.bottomLeft();
  addUserRoiPolygon(polygon);
}

void RoiInteractionState::addUserRoiPolygon(const QPolygon &polygon) {
  if (polygon.size() < 3) {
    return;
  }

  const QRect bounds = polygon.boundingRect().normalized();
  if (bounds.isEmpty()) {
    return;
  }

  m_userRoiPolygons.append(polygon);
  m_roiEnabled = !m_userRoiPolygons.isEmpty();
  qDebug() << "[ROI][VideoWidget] polygon add: points=" << polygon.size()
           << "bounds=" << bounds << "count=" << m_userRoiPolygons.size();
}

bool RoiInteractionState::removeRoiAt(int index) {
  if (index < 0 || index >= m_userRoiPolygons.size()) {
    return false;
  }
  m_userRoiPolygons.removeAt(index);
  m_roiEnabled = !m_userRoiPolygons.isEmpty();
  return true;
}

int RoiInteractionState::roiCount() const { return m_userRoiPolygons.size(); }

void RoiInteractionState::startDrawing(const QSize &frameSize) {
  m_drawingMode = true;
  m_drawingPolygonPoints.clear();
  m_hasHoverPoint = false;
  m_draggingPointIndex = -1;

  if (!frameSize.isEmpty()) {
    const int w = static_cast<int>(frameSize.width() * 0.4);
    const int h = static_cast<int>(frameSize.height() * 0.4);
    const int cx = frameSize.width() / 2;
    const int cy = frameSize.height() / 2;
    m_drawingPolygonPoints << QPoint(cx - w / 2, cy - h / 2)
                           << QPoint(cx + w / 2, cy - h / 2)
                           << QPoint(cx + w / 2, cy + h / 2)
                           << QPoint(cx - w / 2, cy + h / 2);
  }
}

RoiFinishResult RoiInteractionState::finishDrawing() {
  RoiFinishResult result;

  if (!m_drawingMode) {
    return result;
  }

  if (m_drawingPolygonPoints.size() >= 3) {
    QPolygon polygon(m_drawingPolygonPoints);
    addUserRoiPolygon(polygon);
    result.completed = true;
    result.polygon = polygon;
    result.boundingRect = polygon.boundingRect();
  }

  m_drawingMode = false;
  m_drawingPolygonPoints.clear();
  m_hasHoverPoint = false;
  m_draggingPointIndex = -1;
  return result;
}

bool RoiInteractionState::handleMousePress(QMouseEvent *event,
                                           const QSize &widgetSize,
                                           const QSize &frameSize) {
  if (!m_drawingMode || frameSize.isEmpty() ||
      event->button() != Qt::LeftButton) {
    return false;
  }

  const QRect pixRect = displayedPixmapRect(widgetSize, frameSize);
  const QPoint widgetPoint = event->position().toPoint();
  if (!pixRect.contains(widgetPoint)) {
    return false;
  }

  const double sx = static_cast<double>(pixRect.width()) / frameSize.width();
  const double sy = static_cast<double>(pixRect.height()) / frameSize.height();

  auto toWidget = [&](const QPoint &framePoint) -> QPoint {
    return QPoint(pixRect.x() + static_cast<int>(framePoint.x() * sx),
                  pixRect.y() + static_cast<int>(framePoint.y() * sy));
  };

  m_draggingPointIndex = -1;
  for (int i = 0; i < m_drawingPolygonPoints.size(); ++i) {
    const QPoint wp = toWidget(m_drawingPolygonPoints[i]);
    if ((wp - widgetPoint).manhattanLength() < 15) {
      m_draggingPointIndex = i;
      return true;
    }
  }

  return true;
}

bool RoiInteractionState::handleMouseMove(QMouseEvent *event,
                                          const QSize &widgetSize,
                                          const QSize &frameSize) {
  if (!m_drawingMode || frameSize.isEmpty()) {
    return false;
  }

  const QRect pixRect = displayedPixmapRect(widgetSize, frameSize);
  const QPoint widgetPoint = event->position().toPoint();

  if (m_draggingPointIndex >= 0 &&
      m_draggingPointIndex < m_drawingPolygonPoints.size()) {
    m_drawingPolygonPoints[m_draggingPointIndex] =
        mapWidgetToFrame(widgetPoint, widgetSize, frameSize);
    return true;
  }

  return true;
}

bool RoiInteractionState::handleMouseRelease(QMouseEvent *event) {
  if (!m_drawingMode) {
    return false;
  }
  if (event->button() == Qt::LeftButton) {
    m_draggingPointIndex = -1;
    return true;
  }
  return false;
}

void RoiInteractionState::clearHoverPoint() { m_hasHoverPoint = false; }

void RoiInteractionState::paintDrawingOverlay(QWidget *widget,
                                              const QSize &frameSize) const {
  if (!m_drawingMode || frameSize.isEmpty() ||
      m_drawingPolygonPoints.isEmpty()) {
    return;
  }

  const QRect pixRect = displayedPixmapRect(widget->size(), frameSize);
  if (pixRect.isEmpty()) {
    return;
  }

  QPainter painter(widget);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const double sx = static_cast<double>(pixRect.width()) / frameSize.width();
  const double sy = static_cast<double>(pixRect.height()) / frameSize.height();

  auto toWidget = [&](const QPoint &framePoint) -> QPoint {
    return QPoint(pixRect.x() + static_cast<int>(framePoint.x() * sx),
                  pixRect.y() + static_cast<int>(framePoint.y() * sy));
  };

  QPolygon widgetPoly;
  for (const QPoint &pt : m_drawingPolygonPoints) {
    widgetPoly << toWidget(pt);
  }

  QColor cyanAccent(0, 229, 255);
  QColor cyanFill(0, 229, 255, 40);

  painter.setPen(QPen(cyanAccent, 2, Qt::SolidLine));
  painter.setBrush(cyanFill);
  painter.drawPolygon(widgetPoly);

  painter.setBrush(Qt::white);
  painter.setPen(QPen(cyanAccent, 2));
  for (const QPoint &pt : widgetPoly) {
    painter.drawEllipse(pt, 5, 5);
  }
}

const QList<QPolygon> &RoiInteractionState::roiPolygons() const {
  return m_userRoiPolygons;
}

bool RoiInteractionState::roiEnabled() const { return m_roiEnabled; }

bool RoiInteractionState::isDrawing() const { return m_drawingMode; }

QRect RoiInteractionState::displayedPixmapRect(const QSize &widgetSize,
                                               const QSize &frameSize) const {
  if (frameSize.isEmpty()) {
    return QRect();
  }
  const QSize scaledSize = frameSize.scaled(widgetSize, Qt::KeepAspectRatio);
  const int x = (widgetSize.width() - scaledSize.width()) / 2;
  const int y = (widgetSize.height() - scaledSize.height()) / 2;
  return QRect(x, y, scaledSize.width(), scaledSize.height());
}

QPoint RoiInteractionState::mapWidgetToFrame(const QPoint &widgetPoint,
                                             const QSize &widgetSize,
                                             const QSize &frameSize) const {
  if (frameSize.isEmpty()) {
    return QPoint();
  }
  const QRect pixRect = displayedPixmapRect(widgetSize, frameSize);
  if (pixRect.isEmpty()) {
    return QPoint();
  }

  const int clampedX = qBound(pixRect.left(), widgetPoint.x(), pixRect.right());
  const int clampedY = qBound(pixRect.top(), widgetPoint.y(), pixRect.bottom());
  const double nx =
      static_cast<double>(clampedX - pixRect.left()) / pixRect.width();
  const double ny =
      static_cast<double>(clampedY - pixRect.top()) / pixRect.height();

  const int frameX = qBound(0, static_cast<int>(nx * frameSize.width()),
                            frameSize.width() - 1);
  const int frameY = qBound(0, static_cast<int>(ny * frameSize.height()),
                            frameSize.height() - 1);
  return QPoint(frameX, frameY);
}
