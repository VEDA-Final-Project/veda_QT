#include "camera/camerasource.h"

#include "config/config.h"
#include "telegram/telegrambotapi.h"
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QHash>
#include <algorithm>

namespace {
constexpr qint64 kUiFrameStaleMs = 60;
constexpr qint64 kThumbnailFrameStaleMs = 120;
constexpr qint64 kUiRenderIntervalMs = 33;
constexpr qint64 kThumbnailRenderIntervalMs = 200;
constexpr qint64 kOcrFrameStaleMs = 100;
constexpr qint64 kOcrDispatchIntervalMs = 200;
constexpr qint64 kZoneSyncIntervalMs = 500;
constexpr qint64 kRoiSyncIntervalMs = 1000;
constexpr int kIdleTargetFps = 5;
} // namespace

CameraSource::CameraSource(const QString &cameraKey, int cardIndex,
                           QObject *parent)
    : QObject(parent), m_cameraManager(new CameraManager(this)),
      m_ocrCoordinator(new PlateOcrCoordinator(this)),
      m_parkingService(new ParkingService(this)),
      m_cameraKey(cameraKey.trimmed()), m_cardIndex(cardIndex) {
  m_cameraSession.setCameraManager(m_cameraManager);
  m_cameraSession.setDelayMs(Config::instance().defaultDelayMs());

  connect(m_cameraManager, &CameraManager::metadataReceived, this,
          &CameraSource::onMetadataReceived);
  connect(m_cameraManager, &CameraManager::frameCaptured, this,
          &CameraSource::onFrameCaptured);
  connect(m_cameraManager, &CameraManager::ocrFrameCaptured, this,
          &CameraSource::onOcrFrameCaptured);
  connect(m_cameraManager, &CameraManager::logMessage, this,
          [this](const QString &msg) {
            emit logMessage(QString("[CameraSource][Ch %1] %2")
                                .arg(m_cardIndex + 1)
                                .arg(msg));
          });
  connect(m_ocrCoordinator, &PlateOcrCoordinator::ocrReady, this,
          &CameraSource::onOcrResult);
  connect(m_parkingService, &ParkingService::logMessage, this,
          &CameraSource::logMessage);
}

bool CameraSource::initialize(TelegramBotAPI *telegramApi) {
  if (m_initialized) {
    return true;
  }

  if (!m_parkingService) {
    return false;
  }

  QString parkingError;
  if (!m_parkingService->init(&parkingError)) {
    emit logMessage(QString("[Parking][%1] Service init failed: %2")
                        .arg(m_cameraKey, parkingError));
    return false;
  }

  if (telegramApi) {
    m_parkingService->setTelegramApi(telegramApi);
  }
  m_parkingService->setCameraKey(m_cameraKey);
  m_initialized = true;
  return reloadRoi(false);
}

void CameraSource::start() {
  if (m_cameraKey.isEmpty()) {
    return;
  }
  const QString profile = desiredProfile();
  if (!refreshConnectionFromConfig(profile, true)) {
    return;
  }

  if (!m_cameraManager->isDisplayRunning()) {
    m_cameraManager->setTargetFps(m_consumerSizes.isEmpty() ? kIdleTargetFps : 0);
    m_cameraManager->startDisplayOnly();
  }
  updateAnalyticsState();
}

void CameraSource::stop() {
  m_videoReadyNotified = false;
  m_currentObjects.clear();
  if (m_cameraManager) {
    m_cameraManager->stop();
  }
}

void CameraSource::attachDisplayConsumer(int slotId, const QSize &size) {
  m_consumerSizes.insert(slotId, size);
  updateDisplayProfileForConsumers();
}

void CameraSource::detachDisplayConsumer(int slotId) {
  m_consumerSizes.remove(slotId);
  m_analyticsSlots.remove(slotId);
  updateDisplayProfileForConsumers();
  updateAnalyticsState();
}

void CameraSource::updateConsumerSize(int slotId, const QSize &size) {
  if (slotId < 0) {
    return;
  }
  m_consumerSizes.insert(slotId, size);
  updateDisplayProfileForConsumers();
}

void CameraSource::setAnalyticsActive(int slotId, bool active) {
  if (active) {
    m_analyticsSlots.insert(slotId);
  } else {
    m_analyticsSlots.remove(slotId);
  }
  updateAnalyticsState();
}

void CameraSource::updateObjectFilter(const QSet<QString> &disabledTypes) {
  m_disabledTypes = disabledTypes;
  if (m_cameraManager) {
    m_cameraManager->setDisabledObjectTypes(m_disabledTypes);
  }
}

bool CameraSource::reloadRoi(bool writeLog) {
  const Result<RoiService::RoiInitData> initResult = m_roiService.init(m_cameraKey);
  if (!initResult.isOk()) {
    if (writeLog) {
      emit logMessage(
          QString("[ROI][DB] %1 초기화 실패: %2").arg(m_cameraKey, initResult.error));
    }
    return false;
  }

  syncEnabledRoiPolygons();

  if (writeLog) {
    emit logMessage(QString("[ROI][DB] 채널 '%1' ROI %2개 로드 완료")
                        .arg(m_cameraKey)
                        .arg(initResult.data.loadedCount));
  }
  emit roiDataChanged();
  return true;
}

void CameraSource::syncEnabledRoiPolygons() {
  if (!m_parkingService) {
    return;
  }

  const QVector<QJsonObject> &records = m_roiService.records();
  QList<QPolygonF> enabledPolygons;
  QVector<QString> enabledZoneIds;
  enabledPolygons.reserve(records.size());
  enabledZoneIds.reserve(records.size());

  for (const QJsonObject &record : records) {
    const QJsonArray points = record.value("zone_points").toArray();
    if (points.size() < 3) {
      continue;
    }
    QPolygonF polygon;
    polygon.reserve(points.size());
    for (const QJsonValue &value : points) {
      const QJsonObject point = value.toObject();
      polygon << QPointF(point.value("x").toDouble(), point.value("y").toDouble());
    }
    if (polygon.size() < 3) {
      continue;
    }
    enabledPolygons.append(polygon);
    enabledZoneIds.append(record.value("zone_id").toString());
  }

  m_enabledZoneIds = enabledZoneIds;
  m_parkingService->updateRoiPolygons(enabledPolygons);
}

bool CameraSource::isRunning() const {
  return m_cameraManager && m_cameraManager->isRunning();
}

QString CameraSource::cameraKey() const { return m_cameraKey; }

int CameraSource::cardIndex() const { return m_cardIndex; }

QString CameraSource::displayProfile() const { return m_displayProfile; }

QString CameraSource::ocrProfile() const { return m_ocrProfile; }

ParkingService *CameraSource::parkingService() { return m_parkingService; }

const ParkingService *CameraSource::parkingService() const {
  return m_parkingService;
}

RoiService *CameraSource::roiService() { return &m_roiService; }

const RoiService *CameraSource::roiService() const { return &m_roiService; }

const QVector<QJsonObject> &CameraSource::roiRecords() const {
  return m_roiService.records();
}

QList<VehicleState> CameraSource::activeVehicles() const {
  return m_parkingService ? m_parkingService->activeVehicles()
                          : QList<VehicleState>();
}

QList<QPolygonF> CameraSource::normalizedRoiPolygons() const {
  QList<QPolygonF> polygons;
  const QVector<QJsonObject> &records = m_roiService.records();
  polygons.reserve(records.size());
  for (const QJsonObject &record : records) {
    const QJsonArray points = record.value("zone_points").toArray();
    if (points.size() < 3) {
      continue;
    }
    QPolygonF polygon;
    polygon.reserve(points.size());
    for (const QJsonValue &value : points) {
      const QJsonObject point = value.toObject();
      polygon << QPointF(point.value("x").toDouble(), point.value("y").toDouble());
    }
    if (polygon.size() >= 3) {
      polygons.append(polygon);
    }
  }
  return polygons;
}

QStringList CameraSource::roiLabels() const {
  QStringList labels;
  const QVector<QJsonObject> &records = m_roiService.records();
  labels.reserve(records.size());
  for (const QJsonObject &record : records) {
    labels.append(record.value("zone_name").toString().trimmed());
  }
  return labels;
}

void CameraSource::onMetadataReceived(const QList<ObjectInfo> &objects) {
  m_cameraSession.pushMetadata(objects, QDateTime::currentMSecsSinceEpoch());

  if ((!m_roiSyncTimer.isValid() || m_roiSyncTimer.elapsed() >= kRoiSyncIntervalMs) &&
      m_parkingService) {
    m_roiSyncTimer.restart();
    syncEnabledRoiPolygons();
  }

  const auto &cfg = Config::instance();
  if (m_parkingService) {
    m_parkingService->processMetadata(objects, 0, cfg.effectiveWidth(),
                                      cfg.sourceHeight(), 5000);
  }

  if (!m_zoneStatusTimer.isValid() ||
      m_zoneStatusTimer.elapsed() >= kZoneSyncIntervalMs) {
    m_zoneStatusTimer.restart();
    syncZoneOccupancyFromActiveVehicles();
  }
}

void CameraSource::onFrameCaptured(QSharedPointer<cv::Mat> framePtr,
                                   qint64 timestampMs) {
  if (!framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > kUiFrameStaleMs) {
    return;
  }

  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);
  const QList<ObjectInfo> readyMetadata = m_cameraSession.consumeReadyMetadata(nowMs);
  m_currentObjects = readyMetadata;

  if (m_frameRenderTimer.isValid() &&
      m_frameRenderTimer.elapsed() < kUiRenderIntervalMs) {
    if (m_thumbnailRenderTimer.isValid() &&
        m_thumbnailRenderTimer.elapsed() < kThumbnailRenderIntervalMs) {
      return;
    }
  }

  const QImage copiedFrame = qimg.copy();
  if (!m_frameRenderTimer.isValid() ||
      m_frameRenderTimer.elapsed() >= kUiRenderIntervalMs) {
    m_frameRenderTimer.restart();
    emit displayFrameReady(copiedFrame, readyMetadata);
    if (!m_videoReadyNotified) {
      m_videoReadyNotified = true;
      emit videoReady();
    }
  }

  if (!m_thumbnailRenderTimer.isValid() ||
      m_thumbnailRenderTimer.elapsed() >= kThumbnailRenderIntervalMs) {
    m_thumbnailRenderTimer.restart();
    emit thumbnailFrameReady(m_cardIndex, copiedFrame);
  }
}

void CameraSource::onOcrFrameCaptured(QSharedPointer<cv::Mat> framePtr,
                                      qint64 timestampMs) {
  if (!framePtr || framePtr->empty() || m_analyticsSlots.isEmpty()) {
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
      m_ocrDispatchTimer.elapsed() < kOcrDispatchIntervalMs) {
    return;
  }
  m_ocrDispatchTimer.restart();

  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);
  QList<OcrRequest> ocrRequests;
  m_frameRenderer.collectOcrRequests(qimg, m_currentObjects, &ocrRequests);
  for (const OcrRequest &request : ocrRequests) {
    m_ocrCoordinator->requestOcr(request.objectId, request.crop);
  }
}

void CameraSource::onOcrResult(int objectId, const OcrFullResult &result) {
  const QString displayText =
      !result.filtered.isEmpty() ? result.filtered : result.raw;
  emit logMessage(QString("[OCR][%1] ID:%2 Result:%3 (Latency:%4ms)")
                      .arg(m_cameraKey)
                      .arg(objectId)
                      .arg(displayText)
                      .arg(result.latencyMs));

  if (m_parkingService && !result.filtered.isEmpty()) {
    m_parkingService->processOcrResult(objectId, result.filtered);
  }
}

QString CameraSource::bestProfileForSize(const QSize &size) {
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

bool CameraSource::refreshConnectionFromConfig(const QString &displayProfile,
                                               bool reloadConfig) {
  if (!m_cameraManager) {
    return false;
  }

  if (reloadConfig && !Config::instance().load()) {
    emit logMessage("Warning: could not reload config; using existing values.");
  }

  const auto &cfg = Config::instance();
  CameraConnectionInfo connectionInfo;
  connectionInfo.cameraId = m_cameraKey;
  connectionInfo.ip = cfg.cameraIp(m_cameraKey).trimmed();
  connectionInfo.username = cfg.cameraUsername(m_cameraKey).trimmed();
  connectionInfo.password = cfg.cameraPassword(m_cameraKey);
  connectionInfo.profile =
      displayProfile.trimmed().isEmpty() ? cfg.cameraProfile(m_cameraKey).trimmed()
                                         : displayProfile.trimmed();
  if (connectionInfo.profile.isEmpty()) {
    connectionInfo.profile = QStringLiteral("profile2/media.smp");
  }

  connectionInfo.subProfile = cfg.cameraSubProfile(m_cameraKey).trimmed();
  if (connectionInfo.subProfile.isEmpty()) {
    connectionInfo.subProfile = connectionInfo.profile;
  }

  if (!connectionInfo.isValid()) {
    emit logMessage(
        QString("[Camera] '%1' 설정이 유효하지 않습니다. (ip/user)").arg(m_cameraKey));
    return false;
  }

  m_displayProfile = connectionInfo.profile;
  m_idleProfile = cfg.cameraSubProfile(m_cameraKey).trimmed();
  if (m_idleProfile.isEmpty()) {
    m_idleProfile = m_displayProfile;
  }
  m_ocrProfile = connectionInfo.subProfile;
  m_cameraManager->setConnectionInfo(connectionInfo);
  m_cameraManager->setDisabledObjectTypes(m_disabledTypes);
  return true;
}

QString CameraSource::desiredProfile() const {
  if (m_consumerSizes.isEmpty()) {
    return m_idleProfile.isEmpty() ? Config::instance().cameraSubProfile(m_cameraKey)
                                   : m_idleProfile;
  }

  QSize bestSize;
  for (auto it = m_consumerSizes.begin(); it != m_consumerSizes.end(); ++it) {
    if (it.value().width() * it.value().height() >
        bestSize.width() * bestSize.height()) {
      bestSize = it.value();
    }
  }
  return bestProfileForSize(bestSize);
}

void CameraSource::updateDisplayProfileForConsumers() {
  const QString profile = desiredProfile();
  if (profile.isEmpty()) {
    return;
  }

  const bool profileChanged = (profile != m_displayProfile);
  if (!refreshConnectionFromConfig(profile, false)) {
    return;
  }

  if (!m_cameraManager->isDisplayRunning()) {
    start();
    return;
  }

  if (profileChanged) {
    m_cameraManager->setTargetFps(m_consumerSizes.isEmpty() ? kIdleTargetFps : 0);
    m_cameraManager->restartDisplayPipeline();
    emit logMessage(QString("[Camera] %1 display 재연결: %2")
                        .arg(m_cameraKey, profile));
  } else {
    m_cameraManager->setTargetFps(m_consumerSizes.isEmpty() ? kIdleTargetFps : 0);
  }
}

void CameraSource::updateAnalyticsState() {
  if (!m_cameraManager) {
    return;
  }

  if (m_analyticsSlots.isEmpty()) {
    if (m_cameraManager->isAnalyticsRunning()) {
      m_cameraManager->stopAnalytics();
    }
    return;
  }

  if (!m_cameraManager->isDisplayRunning()) {
    start();
  }
  if (!m_cameraManager->isAnalyticsRunning()) {
    m_cameraManager->startAnalytics();
  }
}

void CameraSource::syncZoneOccupancyFromActiveVehicles() {
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
