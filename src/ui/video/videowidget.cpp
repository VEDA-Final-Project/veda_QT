#include "videowidget.h"
#include <QDateTime>
#include <QDebug>
#include <QPainter>
#include <QtGlobal>

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
      m_profileName, nullptr);

  // === QPixmap 변환 없이 QImage를 직접 저장하여 paintEvent에서 그립니다 ===
  m_currentFrame = composed;
  update(); // paintEvent 트리거
}

void VideoWidget::paintEvent(QPaintEvent *event) {
  QWidget::paintEvent(event);

  if (!m_currentFrame.isNull()) {
    QPainter p(this);
    // 위젯 중앙에 QImage를 직접 그립니다 (QPixmap 변환 비용 0)
    const int x = (width() - m_currentFrame.width()) / 2;
    const int y = (height() - m_currentFrame.height()) / 2;
    p.drawImage(x, y, m_currentFrame);
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
