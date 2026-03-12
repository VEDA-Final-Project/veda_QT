#include "camerachannelruntime.h"

#include "camera/camerasource.h"
#include "parking/parkingservice.h"
#include "roi/roiservice.h"
#include "ui/video/videowidget.h"
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <algorithm>

namespace {
constexpr qint64 kUiRenderIntervalMs = 33;
constexpr qint64 kReidRefreshIntervalMs = 300;
} // namespace

CameraChannelRuntime::CameraChannelRuntime(Slot slot,
                                           const QString &channelLabel,
                                           VideoWidget *videoWidget,
                                           const SharedUiRefs &sharedUi,
                                           QObject *parent)
    : QObject(parent), m_slot(slot), m_channelLabel(channelLabel),
      m_videoWidget(videoWidget), m_sharedUi(sharedUi) {}

void CameraChannelRuntime::connectSignals() {
  if (m_signalsConnected || !m_videoWidget) {
    return;
  }

  if (m_slot == Slot::Ch1 && m_sharedUi.avgFpsLabel) {
    connect(m_videoWidget, &VideoWidget::avgFpsUpdated, this,
            [this](double fps) {
              m_sharedUi.avgFpsLabel->setText(
                  QString("최근 1분 평균 FPS: %1").arg(fps, 0, 'f', 1));
            });
  }

  m_signalsConnected = true;
}

void CameraChannelRuntime::shutdown() { deactivate(); }

bool CameraChannelRuntime::activate(CameraSource *source, int cardIndex) {
  m_selectedCardIndex = cardIndex;
  m_videoReadyNotified = false;

  if (!source) {
    clearWidgetState();
    return false;
  }

  bindSource(source);
  if (m_videoWidget) {
    m_videoWidget->setVisible(true);
    m_videoWidget->setProfileName(source->displayProfile());
  }
  source->attachDisplayConsumer(slotId(), m_videoWidget ? m_videoWidget->size()
                                                        : QSize());
  applyRoiDataToWidget();
  refreshReidTable();
  return true;
}

void CameraChannelRuntime::deactivate() {
  m_selectedCardIndex = -1;
  m_videoReadyNotified = false;
  if (m_source) {
    m_source->detachDisplayConsumer(slotId());
    disconnect(m_source, nullptr, this, nullptr);
    m_source = nullptr;
  }
  clearWidgetState();
}

void CameraChannelRuntime::selectCardWithoutStream(int cardIndex) {
  deactivate();
  m_selectedCardIndex = cardIndex;
}

void CameraChannelRuntime::handleResizeProfileChange() {
  if (!m_source || !m_videoWidget) {
    return;
  }
  m_source->updateConsumerSize(slotId(), m_videoWidget->size());
  m_videoWidget->setProfileName(m_source->displayProfile());
}

void CameraChannelRuntime::updateObjectFilter(
    const QSet<QString> &disabledTypes) {
  if (m_source) {
    m_source->updateObjectFilter(disabledTypes);
  }
}

void CameraChannelRuntime::setShowFps(bool show) {
  if (m_videoWidget) {
    m_videoWidget->setShowFps(show);
  }
}

bool CameraChannelRuntime::reloadRoi(bool writeLog) {
  if (!m_source) {
    return false;
  }
  const bool ok = m_source->reloadRoi(writeLog);
  if (ok) {
    applyRoiDataToWidget();
  }
  return ok;
}

void CameraChannelRuntime::syncEnabledRoiPolygons() {
  if (m_source) {
    m_source->syncEnabledRoiPolygons();
  }
}

void CameraChannelRuntime::setReidPanelActive(bool active) {
  m_reidPanelActive = active;
  if (m_reidPanelActive) {
    refreshReidTable();
  }
}

int CameraChannelRuntime::selectedCardIndex() const {
  return m_selectedCardIndex;
}

QString CameraChannelRuntime::cameraKey() const {
  return m_source ? m_source->cameraKey() : QString();
}

QString CameraChannelRuntime::channelLabel() const { return m_channelLabel; }

QString CameraChannelRuntime::displayProfile() const {
  return m_source ? m_source->displayProfile() : QString();
}

QString CameraChannelRuntime::ocrProfile() const {
  return m_source ? m_source->ocrProfile() : QString();
}

VideoWidget *CameraChannelRuntime::videoWidget() const { return m_videoWidget; }

bool CameraChannelRuntime::isRunning() const {
  return m_source && m_source->isRunning();
}

ParkingService *CameraChannelRuntime::parkingService() {
  return m_source ? m_source->parkingService() : nullptr;
}

const ParkingService *CameraChannelRuntime::parkingService() const {
  return m_source ? m_source->parkingService() : nullptr;
}

RoiService *CameraChannelRuntime::roiService() {
  return m_source ? m_source->roiService() : nullptr;
}

const RoiService *CameraChannelRuntime::roiService() const {
  return m_source ? m_source->roiService() : nullptr;
}

const QVector<QJsonObject> &CameraChannelRuntime::roiRecords() const {
  static const QVector<QJsonObject> kEmptyRecords;
  return m_source ? m_source->roiRecords() : kEmptyRecords;
}

QList<VehicleState> CameraChannelRuntime::activeVehicles() const {
  return m_source ? m_source->activeVehicles() : QList<VehicleState>();
}

CameraSource *CameraChannelRuntime::source() const { return m_source; }

void CameraChannelRuntime::onSourceDisplayFrameReady(
    const QImage &image, const QList<ObjectInfo> &objects) {
  if (!m_source || !m_videoWidget || image.isNull()) {
    return;
  }

  if (m_renderTimer.isValid() &&
      m_renderTimer.elapsed() < kUiRenderIntervalMs) {
    return;
  }
  m_renderTimer.restart();

  m_videoWidget->updateMetadata(objects);
  m_videoWidget->updateFrame(image);
  m_videoWidget->setProfileName(m_source->displayProfile());
  refreshReidTable();

  if (m_slot == Slot::Ch1 && !m_videoReadyNotified) {
    m_videoReadyNotified = true;
    emit videoReady();
  }
}

void CameraChannelRuntime::onSourceRoiDataChanged() { applyRoiDataToWidget(); }

void CameraChannelRuntime::onSourceVideoReady() {
  if (m_slot == Slot::Ch1 && !m_videoReadyNotified) {
    m_videoReadyNotified = true;
    emit videoReady();
  }
}

void CameraChannelRuntime::populateReidTable(
    QTableWidget *table, const QList<VehicleState> &vehicleStates,
    int staleTimeoutMs, bool showStaleObjects) {
  if (!table) {
    return;
  }

  table->setRowCount(0);

  QList<VehicleState> sortedVehicles = vehicleStates;
  std::sort(sortedVehicles.begin(), sortedVehicles.end(),
            [](const VehicleState &a, const VehicleState &b) {
              return a.objectId < b.objectId;
            });

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  for (const VehicleState &vehicle : sortedVehicles) {
    if (vehicle.objectId < 0) {
      continue;
    }

    const bool isStale = (nowMs - vehicle.lastSeenMs) > staleTimeoutMs;
    if (isStale && !showStaleObjects) {
      continue;
    }

    const int row = table->rowCount();
    table->insertRow(row);

    const QColor textColor = isStale ? Qt::gray : Qt::black;

    auto *idItem = new QTableWidgetItem(QString::number(vehicle.objectId));
    idItem->setForeground(textColor);
    table->setItem(row, 0, idItem);

    auto *typeItem = new QTableWidgetItem(vehicle.type);
    typeItem->setForeground(textColor);
    table->setItem(row, 1, typeItem);

    auto *plateItem = new QTableWidgetItem(vehicle.plateNumber);
    plateItem->setForeground(textColor);
    table->setItem(row, 2, plateItem);

    auto *scoreItem =
        new QTableWidgetItem(QString::number(vehicle.score, 'f', 2));
    scoreItem->setForeground(textColor);
    table->setItem(row, 3, scoreItem);

    const QRectF &rect = vehicle.boundingBox;
    auto *bboxItem = new QTableWidgetItem(QString("x:%1 y:%2 w:%3 h:%4")
                                              .arg(rect.x(), 0, 'f', 1)
                                              .arg(rect.y(), 0, 'f', 1)
                                              .arg(rect.width(), 0, 'f', 1)
                                              .arg(rect.height(), 0, 'f', 1));
    bboxItem->setForeground(textColor);
    table->setItem(row, 4, bboxItem);
  }
}

void CameraChannelRuntime::bindSource(CameraSource *source) {
  if (m_source == source) {
    return;
  }

  if (m_source) {
    m_source->detachDisplayConsumer(slotId());
    disconnect(m_source, nullptr, this, nullptr);
  }

  m_source = source;
  if (!m_source) {
    return;
  }

  connect(m_source, &CameraSource::displayFrameReady, this,
          &CameraChannelRuntime::onSourceDisplayFrameReady);
  connect(m_source, &CameraSource::roiDataChanged, this,
          &CameraChannelRuntime::onSourceRoiDataChanged);
  connect(m_source, &CameraSource::videoReady, this,
          &CameraChannelRuntime::onSourceVideoReady);
  connect(m_source, &CameraSource::zoneStateChanged, this,
          &CameraChannelRuntime::zoneStateChanged);
}

void CameraChannelRuntime::applyRoiDataToWidget() {
  if (!m_videoWidget) {
    return;
  }

  if (!m_source) {
    m_videoWidget->queueNormalizedRoiPolygons(QList<QPolygonF>(),
                                              QStringList());
    return;
  }

  m_videoWidget->setUserRoi(QRect());
  m_videoWidget->queueNormalizedRoiPolygons(m_source->normalizedRoiPolygons(),
                                            m_source->roiLabels());
}

void CameraChannelRuntime::refreshReidTable() {
  if (!m_reidPanelActive || !m_sharedUi.reidTable || !m_source ||
      !m_source->parkingService()) {
    return;
  }

  if (m_reidTimer.isValid() && m_reidTimer.elapsed() < kReidRefreshIntervalMs) {
    return;
  }
  m_reidTimer.restart();

  const int staleMs = m_sharedUi.staleTimeoutInput
                          ? m_sharedUi.staleTimeoutInput->value()
                          : 1000;
  const bool showStaleObjects = !m_sharedUi.chkShowStaleObjects ||
                                m_sharedUi.chkShowStaleObjects->isChecked();
  populateReidTable(m_sharedUi.reidTable, m_source->activeVehicles(), staleMs,
                    showStaleObjects);
}

void CameraChannelRuntime::clearWidgetState() {
  if (m_videoWidget) {
    m_videoWidget->setVisible(false);
    m_videoWidget->updateMetadata(QList<ObjectInfo>());
    m_videoWidget->queueNormalizedRoiPolygons(QList<QPolygonF>(),
                                              QStringList());
  }
  if (m_reidPanelActive && m_sharedUi.reidTable) {
    m_sharedUi.reidTable->setRowCount(0);
  }
}

int CameraChannelRuntime::slotId() const { return static_cast<int>(m_slot); }
