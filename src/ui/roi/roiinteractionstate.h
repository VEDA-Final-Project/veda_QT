#ifndef ROIINTERACTIONSTATE_H
#define ROIINTERACTIONSTATE_H

#include <QMouseEvent>
#include <QPainter>
#include <QPoint>
#include <QPolygon>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QWidget>

struct RoiFinishResult
{
  bool completed = false;
  QRect boundingRect;
  QPolygon polygon;
};

class RoiInteractionState
{
public:
  void setUserRoi(const QRect &roi);
  void addUserRoi(const QRect &roi);
  void addUserRoiPolygon(const QPolygon &polygon);
  bool removeRoiAt(int index);
  int roiCount() const;
  void startDrawing();
  RoiFinishResult finishDrawing();

  bool handleMousePress(QMouseEvent *event, const QSize &widgetSize, const QSize &frameSize);
  bool handleMouseMove(QMouseEvent *event, const QSize &widgetSize, const QSize &frameSize);
  void paintDrawingOverlay(QWidget *widget, const QSize &frameSize) const;

  const QList<QPolygon> &roiPolygons() const;
  bool roiEnabled() const;

private:
  QRect displayedPixmapRect(const QSize &widgetSize, const QSize &frameSize) const;
  QPoint mapWidgetToFrame(const QPoint &widgetPoint, const QSize &widgetSize,
                          const QSize &frameSize) const;

  QList<QPolygon> m_userRoiPolygons;
  bool m_roiEnabled = false;
  bool m_drawingMode = false;
  QVector<QPoint> m_drawingPolygonPoints;
  QPoint m_hoverFramePoint;
  bool m_hasHoverPoint = false;
};

#endif // ROIINTERACTIONSTATE_H
