#include "camerachannelruntime.h"

#include "infrastructure/camera/camerasource.h"
#include "application/parking/parkingservice.h"
#include "application/roi/roiservice.h"
#include "presentation/widgets/videowidget.h"
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

CameraChannelRuntime::CameraChannelRuntime(Slot slot, VideoWidget *videoWidget,
                                           const SharedUiRefs &sharedUi,
                                           QObject *parent)
    : QObject(parent), m_slot(slot), m_videoWidget(videoWidget),
      m_sharedUi(sharedUi) {}

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

VideoWidget *CameraChannelRuntime::videoWidget() const { return m_videoWidget; }

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

  QSet<int> occupiedRoiIndices;
  if (m_source->parkingService()) {
    for (const VehicleState &state : m_source->activeVehicles()) {
      if (state.occupiedRoiIndex >= 0) {
        occupiedRoiIndices.insert(state.occupiedRoiIndex);
      }
    }
  }

  m_videoWidget->updateMetadata(objects);
  m_videoWidget->setOccupiedRoiIndices(occupiedRoiIndices);
  m_videoWidget->updateFrame(image);
  m_videoWidget->setProfileName(m_source->displayProfile());


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
    QTableWidget *table, int channelId, const QList<VehicleState> &vehicleStates,
    int staleTimeoutMs, bool showStaleObjects) {
  if (!table) {
    return;
  }

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

    const QColor textColor = isStale ? QColor("#94A3B8") : QColor("#FFFFFF");

    auto *chItem = new QTableWidgetItem(QString("Ch %1").arg(channelId));
    chItem->setForeground(textColor);
    table->setItem(row, 0, chItem);


    QString lowerType = vehicle.type.toLower();
    bool isVehicle = (lowerType == "vehicle" || lowerType == "car" || 
                      lowerType == "vehical" || lowerType == "truck" || 
                      lowerType == "bus");

    const QString displayId = isVehicle 
        ? ((vehicle.reidId.isEmpty() || vehicle.reidId == "V---") ? QString("V%1").arg(vehicle.objectId) : vehicle.reidId)
        : QString("-"); // Hide ID for others
        
    auto *idItem = new QTableWidgetItem(displayId);
    idItem->setForeground(textColor);
    table->setItem(row, 1, idItem);
    
    // If we have a reidId, we could also show the original objectId in tooltips or another column
    if (!vehicle.reidId.isEmpty()) {
        idItem->setToolTip(QString("Tracker ID: %1").arg(vehicle.objectId));
    }

    auto *typeItem = new QTableWidgetItem(vehicle.type);
    typeItem->setForeground(textColor);
    table->setItem(row, 2, typeItem);

    auto *plateItem = new QTableWidgetItem(vehicle.plateNumber);
    plateItem->setForeground(textColor);
    table->setItem(row, 3, plateItem);

    auto *scoreItem =
        new QTableWidgetItem(QString::number(vehicle.score, 'f', 2));
    scoreItem->setForeground(textColor);
    table->setItem(row, 4, scoreItem);

    const QRectF &rect = vehicle.boundingBox;
    auto *bboxItem = new QTableWidgetItem(QString("x:%1 y:%2 w:%3 h:%4")
                                              .arg(rect.x(), 0, 'f', 1)
                                              .arg(rect.y(), 0, 'f', 1)
                                              .arg(rect.width(), 0, 'f', 1)
                                              .arg(rect.height(), 0, 'f', 1));
    bboxItem->setForeground(textColor);
    table->setItem(row, 5, bboxItem);
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
  populateReidTable(m_sharedUi.reidTable, slotId(), m_source->activeVehicles(),
                    staleMs, showStaleObjects);

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
