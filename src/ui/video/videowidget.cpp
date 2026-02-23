#include "videowidget.h"
#include <QDebug>
#include <QtGlobal>

VideoWidget::VideoWidget(QWidget *parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);
  setStyleSheet("background-color: black;");
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
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
  m_pendingNormalizedRoiPolygons = normalizedPolygons;
  m_pendingRoiLabels = labels;
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
  if (!m_pendingNormalizedRoiPolygons.isEmpty()) {
    if (index < 0 || index >= m_pendingNormalizedRoiPolygons.size()) {
      return false;
    }
    m_pendingNormalizedRoiPolygons.removeAt(index);
    if (index < m_pendingRoiLabels.size()) {
      m_pendingRoiLabels.removeAt(index);
    }
    return true;
  }

  const bool removed = m_roiState.removeRoiAt(index);
  if (removed) {
    if (index >= 0 && index < m_roiLabels.size()) {
      m_roiLabels.removeAt(index);
    }
    update();
  }
  return removed;
}

int VideoWidget::roiCount() const { return m_roiState.roiCount(); }

const QList<QPolygon> &VideoWidget::roiPolygons() const {
  return m_roiState.roiPolygons();
}

void VideoWidget::startRoiDrawing() {
  m_roiState.startDrawing();
  setCursor(Qt::CrossCursor);
  qDebug() << "[ROI][VideoWidget] polygon drawing mode enabled";
  update();
}

bool VideoWidget::completeRoiDrawing() {
  const int beforeCount = m_roiState.roiCount();
  const RoiFinishResult result = m_roiState.finishDrawing();

  if (result.completed) {
    if (m_roiState.roiCount() > beforeCount) {
      m_roiLabels.append(QString());
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

void VideoWidget::updateFrame(const QImage &frame) { renderFrame(frame); }

void VideoWidget::renderFrame(const QImage &frame) {
  m_lastFrameSize = frame.size();
  if (!m_pendingNormalizedRoiPolygons.isEmpty() && !m_lastFrameSize.isEmpty()) {
    for (int i = 0; i < m_pendingNormalizedRoiPolygons.size(); ++i) {
      const QPolygonF &normalizedPolygon = m_pendingNormalizedRoiPolygons[i];
      QPolygon framePolygon;
      framePolygon.reserve(normalizedPolygon.size());
      for (const QPointF &normPoint : normalizedPolygon) {
        const int frameX =
            qBound(0, static_cast<int>(normPoint.x() * m_lastFrameSize.width()),
                   m_lastFrameSize.width() - 1);
        const int frameY = qBound(
            0, static_cast<int>(normPoint.y() * m_lastFrameSize.height()),
            m_lastFrameSize.height() - 1);
        framePolygon << QPoint(frameX, frameY);
      }
      m_roiState.addUserRoiPolygon(framePolygon);
      m_roiLabels.append(i < m_pendingRoiLabels.size() ? m_pendingRoiLabels[i]
                                                       : QString());
    }
    m_pendingNormalizedRoiPolygons.clear();
    m_pendingRoiLabels.clear();
  }

  const int roiCount = m_roiState.roiCount();
  if (m_roiLabels.size() < roiCount) {
    m_roiLabels.resize(roiCount);
  } else if (m_roiLabels.size() > roiCount) {
    m_roiLabels.resize(roiCount);
  }

  QList<OcrRequest> ocrRequests;
  const QImage composed = m_frameRenderer.compose(
      frame, size(), m_currentObjects, m_roiState.roiPolygons(), m_roiLabels,
      m_roiState.roiEnabled(), &ocrRequests);

  for (const OcrRequest &req : ocrRequests) {
    emit ocrRequested(req.objectId, req.crop);
  }

  setPixmap(QPixmap::fromImage(composed));
}

void VideoWidget::paintEvent(QPaintEvent *event) {
  QLabel::paintEvent(event);
  m_roiState.paintDrawingOverlay(this, m_lastFrameSize);
}

void VideoWidget::mousePressEvent(QMouseEvent *event) {
  if (m_roiState.handleMousePress(event, size(), m_lastFrameSize)) {
    update();
    return;
  }
  QLabel::mousePressEvent(event);
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event) {
  if (m_roiState.handleMouseMove(event, size(), m_lastFrameSize)) {
    update();
    return;
  }
  QLabel::mouseMoveEvent(event);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event) {
  QLabel::mouseReleaseEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event) {
  QLabel::mouseDoubleClickEvent(event);
}
