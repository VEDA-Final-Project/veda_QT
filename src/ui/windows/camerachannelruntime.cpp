#include "camerachannelruntime.h"

#include "config/config.h"
#include "telegram/telegrambotapi.h"
#include "ui/video/videowidget.h"
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QWidget>
#include <algorithm>

namespace {
constexpr qint64 kUiFrameStaleMs = 60;
constexpr qint64 kOcrFrameStaleMs = 100;
constexpr qint64 kUiRenderIntervalMs = 33;
constexpr qint64 kThumbnailRenderIntervalMs = 200;
constexpr qint64 kZoneSyncIntervalMs = 500;
constexpr qint64 kRoiSyncIntervalMs = 1000;
constexpr qint64 kReidRefreshIntervalMs = 300;
} // namespace

CameraChannelRuntime::CameraChannelRuntime(Slot slot,
                                           const QString &channelLabel,
                                           VideoWidget *videoWidget,
                                           const QString &defaultCameraKey,
                                           const SharedUiRefs &sharedUi,
                                           QObject *parent)
    : QObject(parent), m_slot(slot), m_channelLabel(channelLabel),
      m_defaultCameraKey(defaultCameraKey.trimmed()),
      m_cameraKey(defaultCameraKey.trimmed()), m_videoWidget(videoWidget),
      m_sharedUi(sharedUi) {
  m_cameraManager = new CameraManager(this);
  m_ocrCoordinator = new PlateOcrCoordinator(this);
  m_parkingService = new ParkingService(this);
  m_cameraSession.setCameraManager(m_cameraManager);
  m_cameraSession.setDelayMs(Config::instance().defaultDelayMs());
}

void CameraChannelRuntime::connectSignals() {
  if (m_signalsConnected || !m_videoWidget || !m_cameraManager ||
      !m_ocrCoordinator || !m_parkingService) {
    return;
  }

  connect(m_videoWidget, &VideoWidget::ocrRequested, m_ocrCoordinator,
          &PlateOcrCoordinator::requestOcr);
  if (m_slot == Slot::Primary && m_sharedUi.avgFpsLabel) {
    connect(m_videoWidget, &VideoWidget::avgFpsUpdated, this, [this](double fps) {
      m_sharedUi.avgFpsLabel->setText(
          QString("최근 1분 평균 FPS: %1").arg(fps, 0, 'f', 1));
    });
  }

  connect(m_cameraManager, &CameraManager::metadataReceived, this,
          &CameraChannelRuntime::onMetadataReceived);
  connect(m_cameraManager, &CameraManager::frameCaptured, this,
          &CameraChannelRuntime::onFrameCaptured);
  connect(m_cameraManager, &CameraManager::ocrFrameCaptured, this,
          &CameraChannelRuntime::onOcrFrameCaptured);
  connect(m_cameraManager, &CameraManager::logMessage, this,
          &CameraChannelRuntime::logMessage);
  connect(m_ocrCoordinator, &PlateOcrCoordinator::ocrReady, this,
          &CameraChannelRuntime::onOcrResult);
  connect(m_parkingService, &ParkingService::logMessage, this,
          [this](const QString &msg) {
            emit logMessage(QString("[%1] %2").arg(m_channelLabel, msg));
          });

  m_signalsConnected = true;
}

bool CameraChannelRuntime::initializeParkingService() {
  if (!m_parkingService) {
    return false;
  }

  QString parkingError;
  if (!m_parkingService->init(&parkingError)) {
    emit logMessage(
        QString("[Parking][%1] Service init failed: %2")
            .arg(m_channelLabel, parkingError));
    return false;
  }

  m_parkingService->setCameraKey(m_cameraKey);
  return true;
}

void CameraChannelRuntime::setTelegramApi(TelegramBotAPI *telegramApi) {
  if (m_parkingService) {
    m_parkingService->setTelegramApi(telegramApi);
  }
}

void CameraChannelRuntime::shutdown() { m_cameraSession.stop(); }

bool CameraChannelRuntime::activate(const QString &cameraKey, int cardIndex) {
  m_selectedCardIndex = cardIndex;
  m_videoReadyNotified = false;
  m_cameraKey =
      cameraKey.trimmed().isEmpty() ? m_defaultCameraKey : cameraKey.trimmed();

  if (m_parkingService) {
    m_parkingService->setCameraKey(m_cameraKey);
  }
  if (m_videoWidget) {
    m_videoWidget->setVisible(true);
  }

  const QString profile =
      m_videoWidget ? bestProfileForSize(m_videoWidget->size()) : QString();
  if (m_videoWidget) {
    m_videoWidget->setProfileName(profile);
  }

  if (!refreshConnectionFromConfig(profile, true)) {
    return false;
  }

  reloadRoi(false);
  m_cameraSession.playOrRestart();
  return true;
}

void CameraChannelRuntime::deactivate() {
  m_selectedCardIndex = -1;
  m_videoReadyNotified = false;
  m_displayProfile.clear();
  m_ocrProfile.clear();
  m_enabledZoneIds.clear();
  m_cameraKey.clear();
  m_cameraSession.stop();
  clearWidgetState();
  if (m_parkingService) {
    m_parkingService->setCameraKey(QString());
  }
}

void CameraChannelRuntime::selectCardWithoutStream(int cardIndex) {
  deactivate();
  m_selectedCardIndex = cardIndex;
}

void CameraChannelRuntime::handleResizeProfileChange() {
  if (m_selectedCardIndex == -1 || !m_videoWidget || !m_cameraManager ||
      m_cameraKey.trimmed().isEmpty() || !m_cameraManager->isRunning()) {
    return;
  }

  const QString newProfile = bestProfileForSize(m_videoWidget->size());
  if (newProfile.isEmpty() || newProfile == m_displayProfile) {
    return;
  }

  if (refreshConnectionFromConfig(newProfile, false)) {
    m_videoWidget->setProfileName(newProfile);
    m_cameraManager->restartDisplayPipeline();
    emit logMessage(QString("[Camera] %1 리사이즈 재연결: %2")
                        .arg(m_channelLabel, newProfile));
  }
}

void CameraChannelRuntime::updateObjectFilter(
    const QSet<QString> &disabledTypes) {
  if (m_cameraManager) {
    m_cameraManager->setDisabledObjectTypes(disabledTypes);
  }
}

void CameraChannelRuntime::setShowFps(bool show) {
  if (m_videoWidget) {
    m_videoWidget->setShowFps(show);
  }
}

bool CameraChannelRuntime::reloadRoi(bool writeLog) {
  if (!m_videoWidget) {
    return false;
  }

  const Result<RoiService::RoiInitData> initResult = m_roiService.init(m_cameraKey);
  if (!initResult.isOk()) {
    if (writeLog) {
      emit logMessage(QString("[ROI][DB] %1 초기화 실패: %2")
                          .arg(m_channelLabel, initResult.error));
    }
    return false;
  }

  m_videoWidget->setUserRoi(QRect());
  QStringList roiLabels;
  const QVector<QJsonObject> &records = m_roiService.records();
  roiLabels.reserve(records.size());
  for (const QJsonObject &record : records) {
    roiLabels.append(record["zone_name"].toString().trimmed());
  }
  m_videoWidget->queueNormalizedRoiPolygons(initResult.data.normalizedPolygons,
                                            roiLabels);
  syncEnabledRoiPolygonsInternal();

  if (writeLog) {
    emit logMessage(QString("[ROI][DB] %1 채널 '%2' ROI %3개 로드 완료")
                        .arg(m_channelLabel, m_cameraKey)
                        .arg(initResult.data.loadedCount));
  }
  return true;
}

void CameraChannelRuntime::syncEnabledRoiPolygons() {
  syncEnabledRoiPolygonsInternal();
}

void CameraChannelRuntime::setReidPanelActive(bool active) {
  m_reidPanelActive = active;
  if (m_reidPanelActive) {
    refreshReidTable();
  }
}

int CameraChannelRuntime::selectedCardIndex() const { return m_selectedCardIndex; }

QString CameraChannelRuntime::cameraKey() const { return m_cameraKey; }

QString CameraChannelRuntime::channelLabel() const { return m_channelLabel; }

QString CameraChannelRuntime::displayProfile() const { return m_displayProfile; }

QString CameraChannelRuntime::ocrProfile() const { return m_ocrProfile; }

VideoWidget *CameraChannelRuntime::videoWidget() const { return m_videoWidget; }

bool CameraChannelRuntime::isRunning() const {
  return m_cameraManager && m_cameraManager->isRunning();
}

ParkingService *CameraChannelRuntime::parkingService() { return m_parkingService; }

const ParkingService *CameraChannelRuntime::parkingService() const {
  return m_parkingService;
}

RoiService *CameraChannelRuntime::roiService() { return &m_roiService; }

const RoiService *CameraChannelRuntime::roiService() const {
  return &m_roiService;
}

const QVector<QJsonObject> &CameraChannelRuntime::roiRecords() const {
  return m_roiService.records();
}

QList<VehicleState> CameraChannelRuntime::activeVehicles() const {
  return m_parkingService ? m_parkingService->activeVehicles()
                          : QList<VehicleState>();
}

void CameraChannelRuntime::onMetadataReceived(const QList<ObjectInfo> &objects) {
  m_cameraSession.pushMetadata(objects, QDateTime::currentMSecsSinceEpoch());

  if ((!m_roiSyncTimer.isValid() || m_roiSyncTimer.elapsed() >= kRoiSyncIntervalMs) &&
      m_videoWidget && m_parkingService) {
    m_roiSyncTimer.restart();
    syncEnabledRoiPolygonsInternal();
  }

  const auto &cfg = Config::instance();
  const int pruneMs =
      m_sharedUi.pruneTimeoutInput ? m_sharedUi.pruneTimeoutInput->value()
                                   : 5000;
  if (m_parkingService) {
    m_parkingService->processMetadata(objects, 0, cfg.effectiveWidth(),
                                      cfg.sourceHeight(), pruneMs);
  }

  if (!m_zoneStatusTimer.isValid() ||
      m_zoneStatusTimer.elapsed() >= kZoneSyncIntervalMs) {
    m_zoneStatusTimer.restart();
    syncZoneOccupancyFromActiveVehicles();
  }

  refreshReidTable();
}

void CameraChannelRuntime::onFrameCaptured(QSharedPointer<cv::Mat> framePtr,
                                           qint64 timestampMs) {
  if (!m_videoWidget || !framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > kUiFrameStaleMs) {
    return;
  }

  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);
  const QList<ObjectInfo> readyMetadata = m_cameraSession.consumeReadyMetadata(nowMs);
  m_videoWidget->updateMetadata(readyMetadata);

  if (m_renderTimer.isValid() && m_renderTimer.elapsed() < kUiRenderIntervalMs) {
    return;
  }
  m_renderTimer.restart();

  m_videoWidget->updateFrame(qimg);
  if (m_selectedCardIndex >= 0) {
    emit thumbnailFrameReady(m_selectedCardIndex, qimg);
  }

  if (m_slot == Slot::Primary && !m_videoReadyNotified) {
    m_videoReadyNotified = true;
    emit videoReady();
  }
}

void CameraChannelRuntime::onOcrFrameCaptured(QSharedPointer<cv::Mat> framePtr,
                                              qint64 timestampMs) {
  if (!m_videoWidget || !framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > kOcrFrameStaleMs) {
    return;
  }

  if (!m_parkingService || m_parkingService->activeVehicles().isEmpty()) {
    return;
  }

  if (m_ocrDispatchTimer.isValid() &&
      m_ocrDispatchTimer.elapsed() < kThumbnailRenderIntervalMs) {
    return;
  }
  m_ocrDispatchTimer.restart();

  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);
  m_videoWidget->dispatchOcrRequests(qimg);
}

void CameraChannelRuntime::onOcrResult(int objectId,
                                       const OcrFullResult &result) {
  const QString displayText =
      !result.filtered.isEmpty() ? result.filtered : result.raw;
  emit logMessage(QString("[OCR][%1] ID:%2 Result:%3 (Latency:%4ms)")
                      .arg(m_channelLabel)
                      .arg(objectId)
                      .arg(displayText)
                      .arg(result.latencyMs));

  if (m_parkingService && !result.filtered.isEmpty()) {
    m_parkingService->processOcrResult(objectId, result.filtered);
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

bool CameraChannelRuntime::refreshConnectionFromConfig(
    const QString &displayProfile, bool reloadConfig) {
  if (!m_cameraManager) {
    return false;
  }

  if (reloadConfig && !Config::instance().load()) {
    emit logMessage("Warning: could not reload config; using existing values.");
  }

  const auto &cfg = Config::instance();
  const QString selectedKey =
      m_cameraKey.trimmed().isEmpty() ? m_defaultCameraKey : m_cameraKey.trimmed();
  CameraConnectionInfo connectionInfo;
  connectionInfo.cameraId = selectedKey;
  connectionInfo.ip = cfg.cameraIp(selectedKey).trimmed();
  connectionInfo.username = cfg.cameraUsername(selectedKey).trimmed();
  connectionInfo.password = cfg.cameraPassword(selectedKey);

  if (!displayProfile.isEmpty()) {
    connectionInfo.profile = displayProfile;
  } else {
    connectionInfo.profile = cfg.cameraProfile(selectedKey).trimmed();
    if (connectionInfo.profile.isEmpty()) {
      connectionInfo.profile = QStringLiteral("profile2/media.smp");
    }
  }

  connectionInfo.subProfile = cfg.cameraSubProfile(selectedKey).trimmed();
  if (connectionInfo.subProfile.isEmpty()) {
    connectionInfo.subProfile = cfg.cameraProfile(selectedKey).trimmed();
  }
  if (connectionInfo.subProfile.isEmpty()) {
    connectionInfo.subProfile = QStringLiteral("profile2/media.smp");
  }

  if (!connectionInfo.isValid()) {
    emit logMessage(
        QString("[Camera] '%1' 설정이 유효하지 않습니다. (ip/user)").arg(selectedKey));
    return false;
  }

  m_cameraKey = selectedKey;
  m_displayProfile = connectionInfo.profile;
  m_ocrProfile = connectionInfo.subProfile;
  m_cameraManager->setConnectionInfo(connectionInfo);
  return true;
}

QString CameraChannelRuntime::bestProfileForSize(const QSize &size) const {
  const int effectiveWidth = std::min(size.width(), size.height() * 16 / 9);

  if (effectiveWidth > 3072) {
    return QStringLiteral("profile2/media.smp");
  }
  if (effectiveWidth > 2560) {
    return QStringLiteral("profile3/media.smp");
  }
  if (effectiveWidth > 1920) {
    return QStringLiteral("profile4/media.smp");
  }
  if (effectiveWidth > 1280) {
    return QStringLiteral("profile5/media.smp");
  }
  if (effectiveWidth > 640) {
    return QStringLiteral("profile6/media.smp");
  }
  return QStringLiteral("profile7/media.smp");
}

void CameraChannelRuntime::refreshReidTable() {
  if (!m_reidPanelActive || !m_sharedUi.reidTable || !m_parkingService) {
    return;
  }

  if (m_reidTimer.isValid() && m_reidTimer.elapsed() < kReidRefreshIntervalMs) {
    return;
  }
  m_reidTimer.restart();

  const int staleMs =
      m_sharedUi.staleTimeoutInput ? m_sharedUi.staleTimeoutInput->value() : 1000;
  const bool showStaleObjects =
      !m_sharedUi.chkShowStaleObjects || m_sharedUi.chkShowStaleObjects->isChecked();
  populateReidTable(m_sharedUi.reidTable, m_parkingService->activeVehicles(),
                    staleMs, showStaleObjects);
}

void CameraChannelRuntime::clearWidgetState() {
  if (m_videoWidget) {
    m_videoWidget->setVisible(false);
    m_videoWidget->updateMetadata(QList<ObjectInfo>());
  }
  if (m_reidPanelActive && m_sharedUi.reidTable) {
    m_sharedUi.reidTable->setRowCount(0);
  }
}

void CameraChannelRuntime::syncEnabledRoiPolygonsInternal() {
  if (!m_videoWidget || !m_parkingService) {
    return;
  }

  const auto intPolygons = m_videoWidget->roiPolygons();
  const QVector<QJsonObject> &records = m_roiService.records();
  const int count = std::min<int>(intPolygons.size(), records.size());

  QList<QPolygonF> enabledPolygons;
  QVector<QString> enabledZoneIds;
  enabledPolygons.reserve(count);
  enabledZoneIds.reserve(count);

  for (int i = 0; i < count; ++i) {
    enabledPolygons.append(QPolygonF(intPolygons[i]));
    enabledZoneIds.append(records[i].value("zone_id").toString());
  }

  m_enabledZoneIds = enabledZoneIds;
  m_parkingService->updateRoiPolygons(enabledPolygons);
}

void CameraChannelRuntime::syncZoneOccupancyFromActiveVehicles() {
  if (!m_parkingService || m_enabledZoneIds.isEmpty()) {
    return;
  }

  QHash<QString, int> occupancyByZoneId;
  for (const VehicleState &state : m_parkingService->activeVehicles()) {
    const int roiIndex = state.occupiedRoiIndex;
    if (roiIndex < 0 || roiIndex >= m_enabledZoneIds.size()) {
      continue;
    }
    const QString zoneId = m_enabledZoneIds[roiIndex].trimmed();
    if (!zoneId.isEmpty()) {
      occupancyByZoneId.insert(zoneId, occupancyByZoneId.value(zoneId, 0) + 1);
    }
  }

  bool changed = false;
  const QVector<QJsonObject> &records = m_roiService.records();
  for (const QJsonObject &record : records) {
    const QString zoneId = record.value("zone_id").toString().trimmed();
    if (!m_enabledZoneIds.contains(zoneId)) {
      continue;
    }
    const bool currentEmptyState = record.value("zone_enable").toBool(true);
    const bool shouldBeEmpty = occupancyByZoneId.value(zoneId, 0) == 0;
    if (currentEmptyState == shouldBeEmpty) {
      continue;
    }

    const Result<QJsonObject> updateResult =
        m_roiService.setZoneEnabled(zoneId, shouldBeEmpty);
    if (!updateResult.isOk()) {
      emit logMessage(QString("[ROI] 점유 상태 저장 실패 (%1 / %2): %3")
                          .arg(m_roiService.cameraKey(), zoneId,
                               updateResult.error));
      continue;
    }
    changed = true;
  }

  if (changed) {
    emit zoneStateChanged();
  }
}
