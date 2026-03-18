#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include "metadata/metadatathread.h"
#include "ui/roi/roiinteractionstate.h"
#include "ui/video/videoframerenderer.h"
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPolygonF>
#include <QQueue>
#include <QSize>
#include <QStringList>
#include <QWidget>

/**
 * @brief 비디오 렌더링 위젯
 *
 * 비디오 프레임에 메타데이터 오버레이를 그리고 ROI 입력을 처리합니다.
 * OCR 실행 및 메타데이터 동기화는 외부 코디네이터가 담당합니다.
 */

class VideoWidget : public QWidget {
  Q_OBJECT

public:
  explicit VideoWidget(QWidget *parent = nullptr);
  ~VideoWidget() override = default;

  void setUserRoi(const QRect &roi);
  void addUserRoi(const QRect &roi);
  void addUserRoiPolygon(const QPolygon &polygon);
  void queueNormalizedRoiPolygons(const QList<QPolygonF> &normalizedPolygons,
                                  const QStringList &labels = QStringList());
  void setRoiLabelAt(int index, const QString &label);
  bool removeRoiAt(int index);
  int roiCount() const;
  const QList<QPolygon> &roiPolygons() const;
  void startRoiDrawing();
  bool completeRoiDrawing();

  void setZoom(double zoom);
  double zoom() const;
  void panZoom(double dx, double dy);

public slots:
  void updateFrame(const QImage &frame);
  void updateMetadata(const QList<ObjectInfo> &objects);
  void dispatchOcrRequests(const QImage &frame);
  void setShowFps(bool show);
  void setRecording(bool recording);
  void triggerCaptureFeedback();
  void setProfileName(const QString &name) { m_profileName = name; }

signals:
  void ocrRequested(int objectId, const QImage &crop);
  void roiChanged(const QRect &roi);
  void roiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void avgFpsUpdated(double avgFps);

protected:
  void leaveEvent(QEvent *event) override;

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
  QList<QPolygonF>
      m_normalizedRoiPolygons; // Keep the authoritative normalized polygons
  QStringList m_roiLabels;
  QImage m_currentFrame; // QPixmap 변환 없이 직접 그리기 위한 QImage 저장

  bool m_showFps = false;
  double m_currentFps = 0.0;
  QString m_profileName;
  QQueue<qint64> m_fpsHistory1s;
  QQueue<qint64> m_fpsHistory;
  double m_zoomFactor = 1.0;
  double m_zoomCenterX = 0.5; // Normalized 0.0 ~ 1.0 (Center)
  double m_zoomCenterY = 0.5;
  bool m_isRecording = false;
  bool m_isCapturing = false;
  qint64 m_captureStartTime = 0;
  QTimer *m_animTimer = nullptr;
};

#endif // VIDEOWIDGET_H
