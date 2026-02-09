#include "videowidget.h"
#include <QDebug>
#include <QPainter>
#include <utility>

VideoWidget::VideoWidget(QWidget *parent) : QLabel(parent) {
  setAlignment(Qt::AlignCenter);
  setStyleSheet("background-color: black;");

  // OCR 매니저 초기화
  m_ocrManager = new OcrManager();
  const auto &cfg = Config::instance();
  // VideoWidget에서 곧바로 QString으로 init 호출 (메모리 안전함)
  if (!m_ocrManager->init(cfg.tessdataPath(), cfg.ocrLanguage())) {
    qDebug() << "Could not initialize OCR Manager.";
  }

  connect(&m_ocrWatcher, &QFutureWatcher<QString>::finished, this,
          &VideoWidget::onOcrFinished);
}

VideoWidget::~VideoWidget() {
  if (m_ocrWatcher.isRunning()) {
    m_ocrWatcher.waitForFinished();
  }
  delete m_ocrManager;
  m_ocrManager = nullptr;
}

void VideoWidget::setSyncDelay(int delayMs) {
  m_syncDelayMs = qMax(0, delayMs);
}

int VideoWidget::syncDelay() const { return m_syncDelayMs; }

void VideoWidget::updateMetadata(const QList<ObjectInfo> &objects) {
  m_metadataQueue.append(
      qMakePair(QDateTime::currentMSecsSinceEpoch(), objects));
}

void VideoWidget::updateFrame(const QImage &frame) {
  // 싱크 로직: 딜레이 시간이 지난 메타데이터 적용
  qint64 now = QDateTime::currentMSecsSinceEpoch();

  while (!m_metadataQueue.isEmpty()) {
    if (m_metadataQueue.first().first + m_syncDelayMs <= now) {
      m_currentObjects = m_metadataQueue.takeFirst().second;
    } else {
      break;
    }
  }

  renderFrame(frame);
}

void VideoWidget::renderFrame(const QImage &frame) {
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

  // Map source-space rectangles to the current frame size.

  for (const ObjectInfo &obj : std::as_const(m_currentObjects)) {
    const QRectF &srcRect = obj.rect;
    const double x =
        ((srcRect.x() - cropOffsetX) / effectiveWidth) * keyFrame.width();
    const double y = (srcRect.y() / sourceHeight) * keyFrame.height();
    const double w = (srcRect.width() / effectiveWidth) * keyFrame.width();
    const double h = (srcRect.height() / sourceHeight) * keyFrame.height();

    QRect rect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
               static_cast<int>(h));
    painter.drawRect(rect);

    QString text = QString("%1 (ID:%2)").arg(obj.type).arg(obj.id);

    // OCR 처리 (LicensePlate만)
    if (obj.type == "LicensePlate") {
      QRect safeRect = rect.intersected(keyFrame.rect());
      if (!safeRect.isEmpty() && !m_ocrWatcher.isRunning()) {
        QImage roi = keyFrame.copy(safeRect);
        m_processingOcrId = obj.id;

        QFuture<QString> future = QtConcurrent::run(
            [this, roi]() { return m_ocrManager->performOcr(roi); });
        m_ocrWatcher.setFuture(future);
      }
    }

    if (!obj.extraInfo.isEmpty()) {
      text += QString(" [%1]").arg(obj.extraInfo);
    }

    QRect textRect = painter.fontMetrics().boundingRect(text);
    textRect.moveTopLeft(rect.topLeft() - QPoint(0, textRect.height() + 5));

    painter.fillRect(textRect, Qt::black);
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
    painter.setPen(pen);
  }

  if (m_currentObjects.isEmpty()) {
    painter.setPen(Qt::yellow);
    painter.drawText(10, 30, "Waiting for AI Data...");
  }

  painter.end();

  setPixmap(QPixmap::fromImage(keyFrame).scaled(size(), Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
}

void VideoWidget::onOcrFinished() {
  QString result = m_ocrWatcher.result();
  if (!result.isEmpty()) {
    qDebug() << "OID:" << m_processingOcrId << "Type: LicensePlate (Async)"
             << "OCR Result:" << result;
    emit ocrResult(m_processingOcrId, result);
  }
  m_processingOcrId = -1;
}
