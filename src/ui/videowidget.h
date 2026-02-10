#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include "metadatathread.h"
#include "roiinteractionstate.h"
#include "videoframerenderer.h"
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QSize>
#include <QWidget>

/**
 * @brief 비디오 렌더링 위젯
 *
 * 비디오 프레임에 메타데이터 오버레이를 그리고 ROI 입력을 처리합니다.
 * OCR 실행 및 메타데이터 동기화는 외부 코디네이터가 담당합니다.
 */

class VideoWidget : public QLabel
{
  Q_OBJECT

public:
  explicit VideoWidget(QWidget *parent = nullptr);
  ~VideoWidget() override = default;

  void setUserRoi(const QRect &roi);
  void addUserRoi(const QRect &roi);
  void addUserRoiPolygon(const QPolygon &polygon);
  void startRoiDrawing();
  bool completeRoiDrawing();

public slots:
  void updateFrame(const QImage &frame);
  void updateMetadata(const QList<ObjectInfo> &objects);

signals:
  void ocrRequested(int objectId, const QImage &crop);
  void roiChanged(const QRect &roi);
  void roiPolygonChanged(const QPolygon &polygon);

private:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void renderFrame(const QImage &frame);

  QList<ObjectInfo> m_currentObjects;
  QSize m_lastFrameSize;
  RoiInteractionState m_roiState;
  VideoFrameRenderer m_frameRenderer;
};

#endif // VIDEOWIDGET_H
