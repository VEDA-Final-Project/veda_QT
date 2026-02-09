#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include "config.h"
#include "metadatathread.h"
#include "ocrmanager.h"
#include <QDateTime>
#include <QFutureWatcher>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPolygon>
#include <QQueue>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QVector>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

/**
 * @brief 비디오 렌더링 위젯
 *
 * 비디오 프레임에 메타데이터 오버레이를 그리고 OCR 처리를 수행합니다.
 * MainWindow에서 비디오 렌더링 로직을 분리합니다.
 */
class VideoWidget : public QLabel
{
  Q_OBJECT

public:
  explicit VideoWidget(QWidget *parent = nullptr);
  ~VideoWidget();

  void setSyncDelay(int delayMs);
  int syncDelay() const;
  void setUserRoi(const QRect &roi);
  void addUserRoi(const QRect &roi);
  void addUserRoiPolygon(const QPolygon &polygon);
  void startRoiDrawing();
  bool completeRoiDrawing();

public slots:
  void updateFrame(const QImage &frame);
  void updateMetadata(const QList<ObjectInfo> &objects);

signals:
  void ocrResult(int objectId, const QString &result);
  void roiChanged(const QRect &roi);
  void roiPolygonChanged(const QPolygon &polygon);

private slots:
  void onOcrFinished();

private:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  QRect displayedPixmapRect() const;
  QPoint mapWidgetToFrame(const QPoint &widgetPoint) const;
  QRect currentPreviewRoi() const;
  QRegion roiRegionOnFrame(const QRect &frameRect) const;
  void renderFrame(const QImage &frame);
  bool finishPolygonDrawing();

  // 메타데이터 싱크
  QList<ObjectInfo> m_currentObjects;
  QQueue<QPair<qint64, QList<ObjectInfo>>> m_metadataQueue;
  int m_syncDelayMs = 0;

  // OCR (serialized; Tesseract is not used concurrently).
  OcrManager *m_ocrManager = nullptr;
  QFutureWatcher<QString> m_ocrWatcher;
  int m_processingOcrId = -1;
  QList<QPolygon> m_userRoiPolygons;
  bool m_roiEnabled = false;
  QSize m_lastFrameSize;
  bool m_drawingMode = false;
  bool m_isDragging = false;
  QPoint m_dragStartFramePoint;
  QPoint m_dragCurrentFramePoint;
  QVector<QPoint> m_drawingPolygonPoints;
  QPoint m_hoverFramePoint;
  bool m_hasHoverPoint = false;
};

#endif // VIDEOWIDGET_H
