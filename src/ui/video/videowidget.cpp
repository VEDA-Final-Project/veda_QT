#include "videowidget.h"
#include <QDateTime>
#include <QDebug>
#include <QPainter>
#include <QTimer>
#include <QtGlobal>
#include <cmath>

VideoWidget::VideoWidget(QWidget *parent) : QWidget(parent) {
  setStyleSheet("background-color: black;");
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
}

void VideoWidget::setShowFps(bool show) {
  m_showFps = show;
  if (!show) {
    m_fpsHistory1s.clear();
    m_fpsHistory.clear();
    m_currentFps = 0.0;
    emit avgFpsUpdated(0.0);
  }
}


void VideoWidget::setUserRoi(const QRect &roi) {
  m_roiState.setUserRoi(roi);
  m_roiLabels.clear();
  m_roiLabels.resize(m_roiState.roiCount());
  update();
}

void VideoWidget::addUserRoi(const QRect &roi) {
  const int beforeCount = m_roiState.roiCount();
  m_roiState.addUserRoi(roi);
  if (m_roiState.roiCount() > beforeCount) {
    m_roiLabels.append(QString());
  }
  update();
}

void VideoWidget::addUserRoiPolygon(const QPolygon &polygon) {
  const int beforeCount = m_roiState.roiCount();
  m_roiState.addUserRoiPolygon(polygon);
  if (m_roiState.roiCount() > beforeCount) {
    m_roiLabels.append(QString());
  }
  update();
}

void VideoWidget::queueNormalizedRoiPolygons(
    const QList<QPolygonF> &normalizedPolygons, const QStringList &labels) {
  m_normalizedRoiPolygons = normalizedPolygons;
  m_roiLabels = labels;
  while (m_roiState.roiCount() > 0) {
    m_roiState.removeRoiAt(m_roiState.roiCount() - 1);
  }
}

void VideoWidget::setRoiLabelAt(int index, const QString &label) {
  if (index < 0) {
    return;
  }
  if (m_roiLabels.size() <= index) {
    m_roiLabels.resize(index + 1);
  }
  m_roiLabels[index] = label.trimmed();
}

bool VideoWidget::removeRoiAt(int index) {
  if (index >= 0 && index < m_normalizedRoiPolygons.size()) {
    m_normalizedRoiPolygons.removeAt(index);
  }
  if (index >= 0 && index < m_roiLabels.size()) {
    m_roiLabels.removeAt(index);
  }

  const bool removed = m_roiState.removeRoiAt(index);
  if (removed) {
    update();
  }
  return removed;
}

int VideoWidget::roiCount() const { return m_roiState.roiCount(); }

const QList<QPolygon> &VideoWidget::roiPolygons() const {
  return m_roiState.roiPolygons();
}

void VideoWidget::startRoiDrawing() {
  if (m_lastFrameSize.isEmpty()) {
    m_lastFrameSize = QSize(
        1920, 1080); // 비디오 프레임이 없을 경우 기본 크기 적용 (멈춤 방지)
  }
  m_roiState.startDrawing(m_lastFrameSize);
  setCursor(Qt::ArrowCursor);
  qDebug() << "[ROI][VideoWidget] polygon drawing mode enabled";
  update();
}

bool VideoWidget::completeRoiDrawing() {
  const int beforeCount = m_roiState.roiCount();
  const RoiFinishResult result = m_roiState.finishDrawing();

  if (result.completed) {
    if (m_roiState.roiCount() > beforeCount) {
      m_roiLabels.append(QString());
      QPolygonF normPolygon;
      if (!m_lastFrameSize.isEmpty()) {
        for (const QPoint &pt : result.polygon) {
          normPolygon << QPointF(
              static_cast<double>(pt.x()) / m_lastFrameSize.width(),
              static_cast<double>(pt.y()) / m_lastFrameSize.height());
        }
      }
      m_normalizedRoiPolygons.append(normPolygon);
    }
    emit roiChanged(result.boundingRect);
    emit roiPolygonChanged(result.polygon, m_lastFrameSize);
    qDebug() << "[ROI][VideoWidget] polygon drawn: points="
             << result.polygon.size() << "bounds=" << result.boundingRect;
  } else {
    qDebug() << "[ROI][VideoWidget] polygon canceled: need at least 3 points";
  }

  unsetCursor();
  update();
  return result.completed;
}

void VideoWidget::updateMetadata(const QList<ObjectInfo> &objects) {
  m_currentObjects = objects;
}

void VideoWidget::dispatchOcrRequests(const QImage &frame) {
  if (frame.isNull()) {
    return;
  }

  QList<OcrRequest> ocrRequests;
  m_frameRenderer.collectOcrRequests(frame, m_currentObjects, &ocrRequests);
  for (const OcrRequest &req : ocrRequests) {
    emit ocrRequested(req.objectId, req.crop);
  }
}

void VideoWidget::updateFrame(const QImage &frame) { renderFrame(frame); }

void VideoWidget::renderFrame(const QImage &frame) {
  const bool sizeChanged = (m_lastFrameSize != frame.size());
  m_lastFrameSize = frame.size();

  if (sizeChanged || m_roiState.roiCount() != m_normalizedRoiPolygons.size()) {
    while (m_roiState.roiCount() > 0) {
      m_roiState.removeRoiAt(m_roiState.roiCount() - 1);
    }
    if (!m_lastFrameSize.isEmpty()) {
      for (int i = 0; i < m_normalizedRoiPolygons.size(); ++i) {
        const QPolygonF &normalizedPolygon = m_normalizedRoiPolygons[i];
        QPolygon framePolygon;
        framePolygon.reserve(normalizedPolygon.size());
        for (const QPointF &normPoint : normalizedPolygon) {
          const int frameX = qBound(
              0, static_cast<int>(normPoint.x() * m_lastFrameSize.width()),
              m_lastFrameSize.width() - 1);
          const int frameY = qBound(
              0, static_cast<int>(normPoint.y() * m_lastFrameSize.height()),
              m_lastFrameSize.height() - 1);
          framePolygon << QPoint(frameX, frameY);
        }
        m_roiState.addUserRoiPolygon(framePolygon);
      }
    }
  }

  const int roiCount = m_roiState.roiCount();
  if (m_roiLabels.size() != roiCount) {
    m_roiLabels.resize(roiCount);
  }

  // --- FPS Calculation ---
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_fpsHistory1s.enqueue(nowMs);
  m_fpsHistory.enqueue(nowMs);

  while (!m_fpsHistory1s.isEmpty() && nowMs - m_fpsHistory1s.head() > 1000) {
    m_fpsHistory1s.dequeue();
  }
  while (!m_fpsHistory.isEmpty() && nowMs - m_fpsHistory.head() > 60000) {
    m_fpsHistory.dequeue();
  }

  m_currentFps = static_cast<double>(m_fpsHistory1s.size());
  double avgFps = m_fpsHistory.size() / 60.0;
  if (!m_fpsHistory.isEmpty() && (nowMs - m_fpsHistory.head()) < 60000) {
    double elapsedSecs = (nowMs - m_fpsHistory.head()) / 1000.0;
    if (elapsedSecs > 0) {
      avgFps = m_fpsHistory.size() / elapsedSecs;
    }
  }
  emit avgFpsUpdated(avgFps);
  // -----------------------

  const QImage composed = m_frameRenderer.compose(
      frame, size(), m_currentObjects, m_roiState.roiPolygons(), m_roiLabels,
      m_roiState.roiEnabled(), m_showFps, static_cast<int>(m_currentFps),
      m_profileName, m_zoomFactor, m_zoomCenterX, m_zoomCenterY, nullptr);

  // === QPixmap 변환 없이 QImage를 직접 저장하여 paintEvent에서 그립니다 ===
  m_currentFrame = composed;
  update(); // paintEvent 트리거
}

void VideoWidget::paintEvent(QPaintEvent *event) {
  QWidget::paintEvent(event);

  int imgX = 0;
  int imgY = 0;
  int imgW = width();
  int imgH = height();

  if (!m_currentFrame.isNull()) {
    QPainter p(this);
    imgW = m_currentFrame.width();
    imgH = m_currentFrame.height();
    imgX = (width() - imgW) / 2;
    imgY = (height() - imgH) / 2;
    p.drawImage(imgX, imgY, m_currentFrame);
  }

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  // === 録画(REC) 알림 ===
  if (m_isRecording) {
      const int margin = 15;
      const int dotRadius = 6;
      
      // 부드러운 깜빡임 (싸인파 기반)
      qint64 ms = QDateTime::currentMSecsSinceEpoch();
      double alpha = (std::sin(ms / 150.0) + 1.0) / 2.0; // 0.0 ~ 1.0
      int redAlpha = static_cast<int>(100 + 155 * alpha);

      QRect recBox(imgX + margin, imgY + margin, 90, 32);
      
      // 배경 라운드 박스
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(0, 0, 0, 160));
      p.drawRoundedRect(recBox, 6, 6);

      // 빨간 점
      p.setBrush(QColor(255, 0, 0, redAlpha));
      p.drawEllipse(recBox.left() + 10, recBox.center().y() - dotRadius, dotRadius * 2, dotRadius * 2);
      
      // REC 텍스트
      p.setPen(QColor(255, 255, 255, 220));
      QFont recFont = p.font();
      recFont.setPointSize(12);
      recFont.setBold(true);
      p.setFont(recFont);
      p.drawText(recBox.left() + 28, recBox.center().y() + 5, "REC");
      
      // 테두리 글로우 효과
      p.setPen(QPen(QColor(255, 0, 0, redAlpha / 2), 2));
      p.setBrush(Qt::NoBrush);
      p.drawRoundedRect(recBox, 6, 6);
  }

  // === 캡처(CAPTURED) 알림 (셔터 효과) ===
  if (m_isCapturing) {
      qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_captureStartTime;
      const qint64 animDuration = 800; // 800ms
      
      if (elapsed < animDuration) {
          double progress = static_cast<double>(elapsed) / animDuration;
          double opacity = 1.0 - progress; // 서서히 사라짐
          
          // 화면 전체 플래시
          if (elapsed < 100) { 
             p.fillRect(imgX, imgY, imgW, imgH, QColor(255, 255, 255, static_cast<int>(200 * (1.0 - elapsed/100.0))));
          }

          // 카메라 포커스 / 사각 테두리
          int offset = static_cast<int>(20 * progress);
          p.setPen(QPen(QColor(255, 255, 255, static_cast<int>(255 * opacity)), 4));
          p.drawRect(imgX + 10 + offset, imgY + 10 + offset, imgW - 20 - offset*2, imgH - 20 - offset*2);
          
      } else {
          m_isCapturing = false;
      }
  }

  m_roiState.paintDrawingOverlay(this, m_lastFrameSize);
}

void VideoWidget::mousePressEvent(QMouseEvent *event) {
  if (m_roiState.handleMousePress(event, size(), m_lastFrameSize)) {
    update();
    return;
  }
  QWidget::mousePressEvent(event);
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event) {
  if (m_roiState.handleMouseMove(event, size(), m_lastFrameSize)) {
    update();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event) {
  if (m_roiState.handleMouseRelease(event)) {
    update();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  QWidget::mouseDoubleClickEvent(event);
}

void VideoWidget::leaveEvent(QEvent *event) {
  m_roiState.clearHoverPoint();
  update();
  QWidget::leaveEvent(event);
}

void VideoWidget::setZoom(double zoom) {
  m_zoomFactor = qBound(1.0, zoom, 5.0);
  if (m_zoomFactor <= 1.001) {
    m_zoomCenterX = 0.5;
    m_zoomCenterY = 0.5;
  }
  update();
}

double VideoWidget::zoom() const { return m_zoomFactor; }

void VideoWidget::panZoom(double dx, double dy) {
    if (m_zoomFactor <= 1.001) return;

    // 줌 배율이 높을수록 이동 감도를 조절 (정밀 조작 가능하게 함)
    double sensitivity = 0.05 / m_zoomFactor;
    m_zoomCenterX = qBound(0.0, m_zoomCenterX + dx * sensitivity, 1.0);
    m_zoomCenterY = qBound(0.0, m_zoomCenterY + dy * sensitivity, 1.0);
    update();
}

void VideoWidget::setRecording(bool recording) {
  if (!m_animTimer) {
      m_animTimer = new QTimer(this);
      connect(m_animTimer, &QTimer::timeout, this, [this]() {
          if (m_isRecording || m_isCapturing) {
              update();
          } else {
              m_animTimer->stop();
          }
      });
  }

  if (m_isRecording != recording) {
      m_isRecording = recording;
      if (m_isRecording && !m_animTimer->isActive()) {
          m_animTimer->start(33); // ~30fps
      }
      update();
  }
}

void VideoWidget::triggerCaptureFeedback() {
  if (!m_animTimer) {
      m_animTimer = new QTimer(this);
      connect(m_animTimer, &QTimer::timeout, this, [this]() {
          if (m_isRecording || m_isCapturing) {
              update();
          } else {
              m_animTimer->stop();
          }
      });
  }

  m_isCapturing = true;
  m_captureStartTime = QDateTime::currentMSecsSinceEpoch();
  
  if (!m_animTimer->isActive()) {
      m_animTimer->start(33); // ~30fps
  }
  
  update();
}

