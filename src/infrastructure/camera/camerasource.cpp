#include "infrastructure/camera/camerasource.h"
#include <QDebug>

#include "config/config.h"
#include "infrastructure/telegram/telegrambotapi.h"
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QProcessEnvironment>
#include <QJsonValue>
#include <QtConcurrent>
#include <algorithm>

namespace {
constexpr qint64 kUiFrameStaleMs = 120;
constexpr qint64 kThumbnailFrameStaleMs = 300;
constexpr qint64 kUiRenderIntervalMs = 33;
constexpr qint64 kThumbnailRenderIntervalMs = 200;
constexpr qint64 kOcrFrameStaleMs = 250;
constexpr qint64 kOcrDispatchIntervalMs = 200;
constexpr qint64 kZoneSyncIntervalMs = 500;
constexpr qint64 kRoiSyncIntervalMs = 1000;
constexpr int kIdleTargetFps = 5;
constexpr qint64 kDisplayFrameTimeoutMs = 4000;
constexpr qint64 kConnectingTimeoutMs = 5000;
constexpr int kReconnectBaseDelayMs = 1000;
constexpr int kReconnectMaxDelayMs = 30000;

QString normalizedProfileName(const QString &profile) {
  return profile.trimmed().toLower();
}

bool useOfficialMikeySampleMode() {
  const QString value = QProcessEnvironment::systemEnvironment()
                            .value("VEDA_SRTP_USE_OFFICIAL_MIKEY_SAMPLE")
                            .trimmed()
                            .toLower();
  if (value.isEmpty()) {
    return true;
  }
  return value == QStringLiteral("1") || value == QStringLiteral("true") ||
         value == QStringLiteral("yes");
}

QStringList allowedSrtpDiagnosticCameraKeys() {
  return {QStringLiteral("camera"), QStringLiteral("camera2"),
          QStringLiteral("camera3"), QStringLiteral("camera4")};
}

bool isAllowedSrtpDiagnosticCamera(const QString &cameraKey) {
  return allowedSrtpDiagnosticCameraKeys().contains(cameraKey.trimmed());
}
} // namespace

CameraSource::CameraSource(const QString &cameraKey, int cardIndex,
                           QObject *parent)
    : QObject(parent), m_cameraManager(new CameraManager(this)),
      m_ocrCoordinator(new PlateOcrCoordinator(this)),
      m_parkingService(new ParkingService(this)),
      m_cameraKey(cameraKey.trimmed()), m_cardIndex(cardIndex) {
  m_healthTimer = new QTimer(this);
  m_healthTimer->setInterval(1000);
  connect(m_healthTimer, &QTimer::timeout, this, &CameraSource::onHealthCheck);

  m_reconnectTimer = new QTimer(this);
  m_reconnectTimer->setSingleShot(true);
  connect(m_reconnectTimer, &QTimer::timeout, this,
          &CameraSource::onReconnectTimeout);

  m_displayRenderTimer = new QTimer(this);
  m_displayRenderTimer->setInterval(static_cast<int>(kUiRenderIntervalMs));
  m_displayRenderTimer->setTimerType(Qt::PreciseTimer);
  connect(m_displayRenderTimer, &QTimer::timeout, this,
          &CameraSource::onDisplayRenderTick);

  m_thumbnailRenderTimer = new QTimer(this);
  m_thumbnailRenderTimer->setInterval(
      static_cast<int>(kThumbnailRenderIntervalMs));
  connect(m_thumbnailRenderTimer, &QTimer::timeout, this,
          &CameraSource::onThumbnailRenderTick);

  m_ocrDispatchTimer = new QTimer(this);
  m_ocrDispatchTimer->setInterval(static_cast<int>(kOcrDispatchIntervalMs));
  connect(m_ocrDispatchTimer, &QTimer::timeout, this,
          &CameraSource::onOcrDispatchTick);

  m_reidDispatchTimer = new QTimer(this);
  m_reidDispatchTimer->setInterval(800); // ReID 주기를 0.8초로 상향 (렉 방지)
  connect(m_reidDispatchTimer, &QTimer::timeout, this,
          &CameraSource::onReidDispatchTick);

  m_cameraSession.setCameraManager(m_cameraManager);
  m_cameraSession.setDelayMs(Config::instance().defaultDelayMs());

  connect(m_cameraManager, &CameraManager::metadataReceived, this,
          &CameraSource::onMetadataReceived);
  connect(m_cameraManager, &CameraManager::frameCaptured, this,
          &CameraSource::onFrameCaptured);
  connect(
      m_cameraManager, &CameraManager::logMessage, this,
      [this](const QString &msg) {
        emit logMessage(
            QString("[CameraSource][Ch %1] %2").arg(m_cardIndex + 1).arg(msg));
      });
  connect(m_ocrCoordinator, &PlateOcrCoordinator::ocrReady, this,
          &CameraSource::onOcrResult);
  connect(m_ocrCoordinator, &PlateOcrCoordinator::ocrStarted, this,
          [this](int objectId) {
            if (m_parkingService) {
              m_parkingService->processOcrStarted(objectId);
            }
          });
  connect(m_parkingService, &ParkingService::logMessage, this,
          &CameraSource::logMessage);

}

bool CameraSource::initialize(TelegramBotAPI *telegramApi,
                              std::shared_ptr<ReidSession> reidSession) {
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
  m_reidSession = std::move(reidSession);

  m_initialized = true;
  return reloadRoi(false);
}

void CameraSource::start() {
  m_shouldRun = true;
  m_lastDisplayRenderedTimestampMs = 0;
  m_lastThumbnailRenderedTimestampMs = 0;
  m_lastOcrProcessedTimestampMs = 0;
  if (m_cameraKey.isEmpty()) {
    return;
  }
  if (m_displayRenderTimer && !m_displayRenderTimer->isActive()) {
    m_displayRenderTimer->start();
  }
  if (m_thumbnailRenderTimer && !m_thumbnailRenderTimer->isActive()) {
    m_thumbnailRenderTimer->start();
  }
  if (m_ocrDispatchTimer && !m_ocrDispatchTimer->isActive()) {
    m_ocrDispatchTimer->start();
  }
  if (m_reidDispatchTimer && !m_reidDispatchTimer->isActive()) {
    m_reidDispatchTimer->start();
  }
  startDisplayStream(false, true, QStringLiteral("initial start"));
  if (!m_cameraManager->isAnalyticsRunning()) {
    m_cameraManager->startAnalytics();
  }
}

void CameraSource::stop() {
  m_shouldRun = false;
  m_reidProcessing = false;
  m_videoReadyNotified = false;
  m_latestFrameObjects.clear();
  m_latestFramePtr.reset();
  m_latestBufferedFrameTimestampMs = 0;
  m_lastDisplayRenderedTimestampMs = 0;
  m_lastThumbnailRenderedTimestampMs = 0;
  m_lastOcrProcessedTimestampMs = 0;
  clearReconnect();
  if (m_healthTimer) {
    m_healthTimer->stop();
  }
  if (m_displayRenderTimer) {
    m_displayRenderTimer->stop();
  }
  if (m_thumbnailRenderTimer) {
    m_thumbnailRenderTimer->stop();
  }
  if (m_ocrDispatchTimer) {
    m_ocrDispatchTimer->stop();
  }
  if (m_reidDispatchTimer) {
    m_reidDispatchTimer->stop();
  }
  if (m_cameraManager) {
    m_cameraManager->stop();
  }
  setStatus(Status::Stopped);
}

void CameraSource::attachDisplayConsumer(int slotId, const QSize &size) {
  m_consumerSizes.insert(slotId, size);
  updateDisplayProfileForConsumers();
}

void CameraSource::detachDisplayConsumer(int slotId) {
  m_consumerSizes.remove(slotId);
  updateDisplayProfileForConsumers();
}

void CameraSource::updateConsumerSize(int slotId, const QSize &size) {
  if (slotId < 0) {
    return;
  }
  m_consumerSizes.insert(slotId, size);
  updateDisplayProfileForConsumers();
}

void CameraSource::updateObjectFilter(const QSet<QString> &disabledTypes) {
  m_disabledTypes = disabledTypes;
  if (m_cameraManager) {
    m_cameraManager->setDisabledObjectTypes(m_disabledTypes);
  }
}

bool CameraSource::reloadRoi(bool writeLog) {
  const Result<RoiService::RoiInitData> initResult =
      m_roiService.init(m_cameraKey);
  if (!initResult.isOk()) {
    if (writeLog) {
      emit logMessage(QString("[ROI][DB] %1 초기화 실패: %2")
                          .arg(m_cameraKey, initResult.error));
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
  QStringList enabledZoneNames;
  QVector<QString> enabledZoneIds;
  enabledPolygons.reserve(records.size());
  enabledZoneNames.reserve(records.size());
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
      polygon << QPointF(point.value("x").toDouble(),
                         point.value("y").toDouble());
    }
    if (polygon.size() < 3) {
      continue;
    }
    enabledPolygons.append(polygon);
    const QString zoneName = record.value("zone_name").toString().trimmed();
    enabledZoneNames.append(zoneName.isEmpty() ? record.value("zone_id").toString()
                                               : zoneName);
    enabledZoneIds.append(record.value("zone_id").toString());
  }

  m_enabledZoneIds = enabledZoneIds;
  m_parkingService->updateRoiPolygons(enabledPolygons, enabledZoneNames,
                                      enabledZoneIds.toList());
}

bool CameraSource::isRunning() const {
  return m_cameraManager && m_cameraManager->isRunning();
}

QString CameraSource::cameraKey() const { return m_cameraKey; }

int CameraSource::cardIndex() const { return m_cardIndex; }

QString CameraSource::displayProfile() const { return m_displayProfile; }

CameraSource::Status CameraSource::status() const { return m_status; }

qint64 CameraSource::lastFrameTimestampMs() const {
  return m_lastFrameTimestampMs;
}

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
      polygon << QPointF(point.value("x").toDouble(),
                         point.value("y").toDouble());
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

void CameraSource::onMetadataReceived(const QList<ObjectInfo> &objects,
                                      qint64 timestampMs) {
  m_cameraSession.pushMetadata(objects, timestampMs);

  if ((!m_roiSyncTimer.isValid() ||
       m_roiSyncTimer.elapsed() >= kRoiSyncIntervalMs) &&
      m_parkingService) {
    m_roiSyncTimer.restart();
    syncEnabledRoiPolygons();
  }

  // Metadata is now pushed to session and processed in onFrameCaptured at 30fps
  // for sync. if (m_parkingService) {
  //   m_parkingService->processMetadata(objects, 0, cfg.effectiveWidth(),
  //                                     cfg.sourceHeight(), 5000);
  // }

}

void CameraSource::onFrameCaptured(QSharedPointer<cv::Mat> framePtr,
                                   qint64 timestampMs) {
  if (!framePtr || framePtr->empty()) {
    return;
  }

  emit rawFrameReady(m_cardIndex, framePtr, timestampMs);

  m_lastFrameTimestampMs = timestampMs;
  if (m_healthTimer && !m_healthTimer->isActive()) {
    m_healthTimer->start();
  }
  clearReconnect();
  m_reconnectAttempt = 0;
  setStatus(Status::Live);

  const QList<ObjectInfo> readyMetadata =
      m_cameraSession.consumeReadyMetadata(timestampMs);
  m_latestFramePtr = framePtr;
  m_latestFrameObjects = readyMetadata;
  m_latestBufferedFrameTimestampMs = timestampMs;

  // Keep parking state moving even when the metadata frame is empty.
  if (m_parkingService) {
    const auto &cfg = Config::instance();
    m_parkingService->processMetadata(readyMetadata, 0, cfg.effectiveWidth(),
                                      cfg.sourceHeight(), 5000);

    if (!m_zoneStatusTimer.isValid() ||
        m_zoneStatusTimer.elapsed() >= kZoneSyncIntervalMs) {
      m_zoneStatusTimer.restart();
      syncZoneOccupancyFromActiveVehicles();
    }
  }
}

void CameraSource::onDisplayRenderTick() {
  if (!m_shouldRun || !m_latestFramePtr || m_latestFramePtr->empty()) {
    return;
  }

  const qint64 timestampMs = m_latestBufferedFrameTimestampMs;
  if (timestampMs <= m_lastDisplayRenderedTimestampMs) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > kUiFrameStaleMs) {
    return;
  }

  const auto framePtr = m_latestFramePtr;
  QList<ObjectInfo> readyMetadata = m_latestFrameObjects;

  // Decorate objects with persistent ReID IDs from ParkingService
  if (m_parkingService) {
    for (ObjectInfo &obj : readyMetadata) {
      VehicleState vs = m_parkingService->getVehicleState(obj.id);
      if (vs.objectId >= 0 && !vs.reidId.isEmpty()) {
        obj.reidId = vs.reidId;
      }
    }
  }
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_BGR888);
  const QImage copiedFrame = qimg.copy(); // Keep copy for UI stability

  m_lastDisplayRenderedTimestampMs = timestampMs;
  emit displayFrameReady(copiedFrame, readyMetadata);
  if (!m_videoReadyNotified) {
    m_videoReadyNotified = true;
    emit videoReady();
  }
}

void CameraSource::onThumbnailRenderTick() {
  if (!m_shouldRun || !m_latestFramePtr || m_latestFramePtr->empty()) {
    return;
  }

  const qint64 timestampMs = m_latestBufferedFrameTimestampMs;
  if (timestampMs <= m_lastThumbnailRenderedTimestampMs) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > kThumbnailFrameStaleMs) {
    return;
  }

  const auto framePtr = m_latestFramePtr;
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_BGR888);

  m_lastThumbnailRenderedTimestampMs = timestampMs;
  emit thumbnailFrameReady(m_cardIndex, qimg.copy());
}

void CameraSource::onOcrDispatchTick() {
  if (!m_shouldRun || !m_latestFramePtr || m_latestFramePtr->empty()) {
    return;
  }
  if (!m_parkingService || m_parkingService->activeVehicles().isEmpty()) {
    return;
  }

  const qint64 timestampMs = m_latestBufferedFrameTimestampMs;
  if (timestampMs <= m_lastOcrProcessedTimestampMs) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > kOcrFrameStaleMs) {
    return;
  }

  const auto framePtr = m_latestFramePtr;
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_BGR888);
  QList<OcrRequest> ocrRequests;
  m_frameRenderer.collectOcrRequests(qimg, m_latestFrameObjects, &ocrRequests);
  for (const OcrRequest &request : ocrRequests) {
    m_ocrCoordinator->requestOcr(request.objectId, request.crop);
  }
  m_lastOcrProcessedTimestampMs = timestampMs;
}

void CameraSource::onReidDispatchTick() {
  if (!m_shouldRun || m_reidProcessing || !m_latestFramePtr ||
      m_latestFramePtr->empty() || m_latestFrameObjects.isEmpty()) {
    return;
  }

  const std::shared_ptr<ReidSession> reidSession = m_reidSession;
  if (!reidSession || !reidSession->isReady()) {
    return;
  }

  m_reidProcessing = true;
  QList<ObjectInfo> objects = m_latestFrameObjects;
  QSharedPointer<cv::Mat> framePtr = m_latestFramePtr;

  QPointer<CameraSource> self(this);
  (void)QtConcurrent::run([self, objects, framePtr, reidSession]() {
    if (!self || !self->m_shouldRun) {
      if (self) {
        QMetaObject::invokeMethod(
            self,
            [self]() {
              if (self) {
                self->m_reidProcessing = false;
              }
            },
            Qt::QueuedConnection);
      }
      return;
    }

    QList<ObjectInfo> processedObjects = objects;
    cv::Mat frame = *framePtr; // Swallow copy (ref-counted)
    const int frameW = frame.cols;
    const int frameH = frame.rows;

    const auto &cfg = Config::instance();
    const double srcW_cfg = std::max(1.0, static_cast<double>(cfg.sourceWidth()));
    const double srcH_cfg = std::max(1.0, static_cast<double>(cfg.sourceHeight()));

    bool extractedAny = false;
    int vehicleCount = 0;

    for (ObjectInfo &obj : processedObjects) {
      if (!self || !self->m_shouldRun) {
        break;
      }
      bool isVehicle = isVehicleType(obj.type);
      if (!isVehicle)
        continue;
      vehicleCount++;

      // Use same scaling as VideoFrameRenderer to ensure correct cropping
      const double srcX = (obj.rect.x() / srcW_cfg) * frameW;
      const double srcY = (obj.rect.y() / srcH_cfg) * frameH;
      const double srcW = (obj.rect.width() / srcW_cfg) * frameW;
      const double srcH = (obj.rect.height() / srcH_cfg) * frameH;

      cv::Rect roi(static_cast<int>(srcX), static_cast<int>(srcY),
                   static_cast<int>(srcW), static_cast<int>(srcH));
      roi &= cv::Rect(0, 0, frameW, frameH);

      if (roi.width < 24 || roi.height < 24 || roi.area() <= 0) {
        continue;
      }

      // Partial Clone: Only clone the small vehicle area, not the whole 4K frame!
      cv::Mat vehicleCrop = frame(roi).clone(); 
      obj.reidFeatures = reidSession->extract(vehicleCrop);

      if (!obj.reidFeatures.empty()) {
        extractedAny = true;
      }
    }

    // Update ReID features asynchronously without blocking or slowing down main
    // tracking (30fps)
    if (self) {
      QMetaObject::invokeMethod(
          self,
          [self, processedObjects]() {
            if (!self) {
              return;
            }
            if (self->m_parkingService) {
              self->m_parkingService->updateReidFeatures(processedObjects);
            }
            self->m_reidProcessing = false;
          },
          Qt::QueuedConnection);
    }
  });
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

QString CameraSource::ocrMasterProfile() {
  const QString profile = Config::instance().defaultCameraProfile().trimmed();
  return profile.isEmpty() ? QStringLiteral("profile6/media.smp") : profile;
}

int CameraSource::profileRank(const QString &profile) {
  const QString normalized = normalizedProfileName(profile);
  if (normalized == QStringLiteral("profile2/media.smp")) {
    return 2;
  }
  if (normalized == QStringLiteral("profile3/media.smp")) {
    return 3;
  }
  if (normalized == QStringLiteral("profile4/media.smp")) {
    return 4;
  }
  if (normalized == QStringLiteral("profile5/media.smp")) {
    return 5;
  }
  if (normalized == QStringLiteral("profile6/media.smp")) {
    return 6;
  }
  if (normalized == QStringLiteral("profile7/media.smp")) {
    return 7;
  }
  return 99;
}

QString CameraSource::higherQualityProfile(const QString &a, const QString &b) {
  if (a.trimmed().isEmpty()) {
    return b;
  }
  if (b.trimmed().isEmpty()) {
    return a;
  }
  return profileRank(a) <= profileRank(b) ? a : b;
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
  connectionInfo.allowedFingerprints = cfg.cameraPinnedSha256(m_cameraKey);
  connectionInfo.srtpEnabled = cfg.cameraSrtpEnabled(m_cameraKey);

  // 공식 MIKEY 샘플 검증 중에는 허용된 카메라만 SRTP를 타게 해서
  // RTSP 요청/응답 흐름을 단계적으로 확장합니다.
  if (useOfficialMikeySampleMode() && connectionInfo.srtpEnabled &&
      !isAllowedSrtpDiagnosticCamera(m_cameraKey)) {
    connectionInfo.srtpEnabled = false;
  }

  connectionInfo.profile = displayProfile.trimmed().isEmpty()
                               ? cfg.cameraProfile(m_cameraKey).trimmed()
                               : displayProfile.trimmed();
  if (connectionInfo.profile.isEmpty()) {
    connectionInfo.profile = cfg.defaultCameraProfile();
  }
  const QString configuredVideoProfile =
      cfg.cameraProfile(m_cameraKey).trimmed();
  connectionInfo.profile = higherQualityProfile(
      connectionInfo.profile,
      higherQualityProfile(configuredVideoProfile, ocrMasterProfile()));

  connectionInfo.subProfile = cfg.cameraSubProfile(m_cameraKey).trimmed();
  if (connectionInfo.subProfile.isEmpty()) {
    connectionInfo.subProfile = cfg.cameraProfile(m_cameraKey).trimmed();
  }
  if (connectionInfo.subProfile.isEmpty()) {
    connectionInfo.subProfile = cfg.defaultCameraSubProfile();
  }

  if (!connectionInfo.isValid()) {
    emit logMessage(QString("[Camera] '%1' 설정이 유효하지 않습니다. (ip/user)")
                        .arg(m_cameraKey));
    return false;
  }

  m_displayProfile = connectionInfo.profile;
  m_cameraManager->setConnectionInfo(connectionInfo);
  m_cameraManager->setDisabledObjectTypes(m_disabledTypes);
  return true;
}

QString CameraSource::desiredProfile() const {
  QSize bestSize;
  for (auto it = m_consumerSizes.begin(); it != m_consumerSizes.end(); ++it) {
    if (it.value().width() * it.value().height() >
        bestSize.width() * bestSize.height()) {
      bestSize = it.value();
    }
  }
  const QString sizeDrivenProfile =
      bestSize.isEmpty() ? QString() : bestProfileForSize(bestSize);
  const QString configuredProfile =
      Config::instance().cameraProfile(m_cameraKey).trimmed();
  return higherQualityProfile(
      higherQualityProfile(sizeDrivenProfile, configuredProfile),
      ocrMasterProfile());
}

void CameraSource::updateDisplayProfileForConsumers() {
  const QString profile = desiredProfile();
  if (profile.isEmpty()) {
    return;
  }

  const bool profileChanged = (profile != m_displayProfile);
  if (!refreshConnectionFromConfig(profile, false)) {
    scheduleReconnect(QStringLiteral("config refresh failed"));
    return;
  }

  if (!m_cameraManager->isDisplayRunning()) {
    startDisplayStream(false, false, QStringLiteral("display was not running"));
    return;
  }

  if (profileChanged) {
    startDisplayStream(true, false, QStringLiteral("profile change"));
  } else {
    m_cameraManager->setTargetFps(m_consumerSizes.isEmpty() ? kIdleTargetFps
                                                            : 0);
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
      emit logMessage(
          QString("[ROI] 점유 상태 저장 실패 (%1 / %2): %3")
              .arg(m_roiService.cameraKey(), zoneId, updateResult.error));
      continue;
    }
    changed = true;
  }

  if (changed) {
    emit zoneStateChanged();
  }
}

void CameraSource::onHealthCheck() {
  if (!m_shouldRun) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (m_status == Status::Connecting) {
    if (m_lastStartAttemptMs > 0 &&
        (nowMs - m_lastStartAttemptMs) >= kConnectingTimeoutMs) {
      scheduleReconnect(QStringLiteral("display connect timeout"));
    }
    return;
  }

  if (m_status != Status::Live) {
    return;
  }

  if (m_lastFrameTimestampMs <= 0 ||
      (nowMs - m_lastFrameTimestampMs) >= kDisplayFrameTimeoutMs) {
    scheduleReconnect(QStringLiteral("display frame timeout"));
  }
}

void CameraSource::onReconnectTimeout() {
  if (!m_shouldRun) {
    return;
  }
  startDisplayStream(true, true, QStringLiteral("backoff reconnect"));
}

void CameraSource::setStatus(Status status, const QString &detail) {
  if (m_status == status && m_statusDetail == detail) {
    return;
  }

  m_status = status;
  m_statusDetail = detail;
  emit statusChanged(m_cardIndex, m_status, m_statusDetail);
}

void CameraSource::scheduleReconnect(const QString &reason) {
  if (!m_shouldRun || !m_cameraManager) {
    return;
  }

  const int delayMs =
      std::min(kReconnectBaseDelayMs << std::min(m_reconnectAttempt, 5),
               kReconnectMaxDelayMs);
  ++m_reconnectAttempt;

  if (m_healthTimer && !m_healthTimer->isActive()) {
    m_healthTimer->start();
  }

  if (m_cameraManager->isDisplayRunning()) {
    m_cameraManager->restartDisplayPipeline();
  }

  setStatus(Status::Error,
            QString("%1; retry in %2 ms").arg(reason).arg(delayMs));
  emit logMessage(QString("[Camera] %1 display reconnect scheduled: %2 (%3 ms)")
                      .arg(m_cameraKey, reason)
                      .arg(delayMs));

  if (m_reconnectTimer) {
    m_reconnectTimer->start(delayMs);
  }
}

void CameraSource::clearReconnect() {
  if (m_reconnectTimer && m_reconnectTimer->isActive()) {
    m_reconnectTimer->stop();
  }
}

void CameraSource::startDisplayStream(bool forceRestart, bool reloadConfig,
                                      const QString &reason) {
  if (!m_shouldRun || !m_cameraManager) {
    return;
  }

  const QString profile = desiredProfile();
  if (!refreshConnectionFromConfig(profile, reloadConfig)) {
    setStatus(Status::Error, QStringLiteral("invalid camera config"));
    scheduleReconnect(QStringLiteral("invalid camera config"));
    return;
  }

  m_lastStartAttemptMs = QDateTime::currentMSecsSinceEpoch();
  m_cameraManager->setTargetFps(m_consumerSizes.isEmpty() ? kIdleTargetFps : 0);

  setStatus(Status::Connecting, reason);
  if (m_healthTimer && !m_healthTimer->isActive()) {
    m_healthTimer->start();
  }

  if (forceRestart && m_cameraManager->isDisplayRunning()) {
    m_cameraManager->restartDisplayPipeline();
    emit logMessage(QString("[Camera] %1 display 재연결: %2 (%3)")
                        .arg(m_cameraKey, m_displayProfile, reason));
    return;
  }

  if (!m_cameraManager->isDisplayRunning()) {
    m_cameraManager->startDisplayOnly();
    emit logMessage(QString("[Camera] %1 display 연결 시작: %2 (%3)")
                        .arg(m_cameraKey, m_displayProfile, reason));
  }
}
