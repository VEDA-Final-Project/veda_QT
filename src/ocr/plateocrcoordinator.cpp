#include "ocr/plateocrcoordinator.h"
#include "config/config.h"
#include <QDebug>

PlateOcrCoordinator::PlateOcrCoordinator(QObject *parent) : QObject(parent) {
  const auto &cfg = Config::instance();
  if (!m_ocrManager.init(cfg.tessdataPath(), cfg.ocrLanguage())) {
    qDebug() << "Could not initialize OCR Manager.";
  }

  connect(&m_watcher, &QFutureWatcher<OcrFullResult>::finished, this,
          &PlateOcrCoordinator::onOcrFinished);
}

PlateOcrCoordinator::~PlateOcrCoordinator() {
  m_shuttingDown = true;
  m_pending.clear();
  m_inflightObjectIds.clear();
  if (m_watcher.isRunning()) {
    m_watcher.cancel();
    m_watcher.waitForFinished();
  }
}

void PlateOcrCoordinator::requestOcr(int objectId, const QImage &crop) {
  if (m_shuttingDown) {
    return;
  }

  if (objectId < 0 || crop.isNull()) {
    return;
  }

  if (m_inflightObjectIds.contains(objectId)) {
    return;
  }

  m_inflightObjectIds.insert(objectId);
  m_pending.append(PendingOcr{objectId, crop});
  startNext();
}

void PlateOcrCoordinator::onOcrFinished() {
  if (m_shuttingDown) {
    return;
  }

  const OcrFullResult result = m_watcher.result();
  const int objectId = m_runningObjectId;

  m_inflightObjectIds.remove(objectId);
  m_runningObjectId = -1;

  if (!result.filtered.isEmpty()) {
    emit ocrReady(objectId, result);
  }

  startNext();
}

void PlateOcrCoordinator::startNext() {
  if (m_shuttingDown) {
    return;
  }

  if (m_watcher.isRunning() || m_pending.isEmpty()) {
    return;
  }

  const PendingOcr next = m_pending.dequeue();
  m_runningObjectId = next.objectId;

  QFuture<OcrFullResult> future = QtConcurrent::run(
      [this, crop = next.crop]() { return m_ocrManager.performOcr(crop); });
  m_watcher.setFuture(future);
}
