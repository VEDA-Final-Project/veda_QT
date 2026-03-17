#include "parkingservice.h"
#include "../telegram/telegrambotapi.h"
#include "vehicletracker.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>


namespace {
constexpr qint64 kEntryPersistDelayMs = 5000;
}

ParkingService::ParkingService(QObject *parent) : QObject(parent) {}

void ParkingService::processOcrStarted(int objectId)
{
  m_tracker.setPlateNumber(objectId, QStringLiteral("인식중.."));
}


bool ParkingService::init(QString *errorMessage)
{
  return m_repository.init(errorMessage);
}

void ParkingService::setTelegramApi(TelegramBotAPI *api)
{
  m_telegram = api;
}

void ParkingService::setCameraKey(const QString &cameraKey) 
{
  const QString trimmed = cameraKey.trimmed();
  m_cameraKey = trimmed.isEmpty() ? QStringLiteral("camera") : trimmed;
  // 접두어 통일 로직: 어떤 이름이 오든 숫자만 추출하여 'C + 숫자' 형태로 강제 (예: CCTV1 -> C1)
  QRegularExpression re("(\\d+)");
  QRegularExpressionMatch match = re.match(m_cameraKey);
  
  QString prefix;
  if (match.hasMatch()) {
    prefix = "C" + match.captured(1); 
  } else if (m_cameraKey.toLower().contains("camera")) {
    // 'camera', 'default_camera' 등 숫자가 없지만 명백한 기본 채널인 경우 C1 부여
    prefix = "C1";
  } else {
    // 숫자가 없고 camera라는 단어도 없는 경우 첫 글자 대문자 사용 (fallback)
    prefix = m_cameraKey.left(1).toUpper();
    if (prefix.isEmpty()) prefix = "C1"; // 완전 빈 값인 경우에도 C1로 대접
  }
  
  m_tracker.setIdPrefix(prefix);
  
  QString debugMsg = QString("[Parking] Camera Key: '%1' -> Prefix: '%2'")
                        .arg(m_cameraKey, prefix);
  emit logMessage(debugMsg);
  qDebug() << debugMsg;
}

QString ParkingService::cameraKey() const 
{
  return m_cameraKey; 
}

void ParkingService::updateRoiPolygons(const QList<QPolygonF> &polygons,
                                       const QStringList &zoneNames) 
{
  m_tracker.setRoiPolygons(polygons);
  m_roiZoneNames = zoneNames;
}

void ParkingService::processMetadata(const QList<ObjectInfo> &objects,
                                     int cropOffsetX, int effectiveWidth,
                                     int sourceHeight, qint64 pruneTimeoutMs) {
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  auto newEntries = m_tracker.update(objects, cropOffsetX, effectiveWidth,
                                     sourceHeight, nowMs, pruneTimeoutMs);

  for (const auto &vs : newEntries) {
    if (!vs.ocrRequested) {
      QString lowerType = vs.type.toLower();
      bool isVehicle = (lowerType == "vehicle" || lowerType == "car" ||
                        lowerType == "vehical" || lowerType == "truck" ||
                        lowerType == "bus");
      const QString displayId =
          isVehicle ? (vs.reidId.isEmpty() ? "V---" : vs.reidId) : QString();

      QString logMsg = isVehicle
                           ? QString("[Parking] Vehicle ID:%1 entered ROI #%2")
                                 .arg(displayId)
                                 .arg(vs.occupiedRoiIndex + 1)
                           : QString("[Parking] %1 entered ROI #%2")
                                 .arg(vs.type)
                                 .arg(vs.occupiedRoiIndex + 1);

      emit logMessage(logMsg);
      handleNewEntry(vs);
    }
  }

  // 보정 로직: ROI 안에 있으나 통지가 아직 안 된 차량 재검색
  const auto &activeVehicles = m_tracker.vehicles();
  for (auto it = activeVehicles.cbegin(); it != activeVehicles.cend(); ++it) {
    const VehicleState &vs = it.value();
    if (vs.occupiedRoiIndex >= 0 && !vs.notified && vs.roiEntryMs > 0 && 
        (nowMs - vs.roiEntryMs) >= kEntryPersistDelayMs) {
      handleNewEntry(vs);
    }
  }
      handleNewEntry(vs);
    }
  }

  // 새 진입 이벤트를 놓쳤더라도, 현재 ROI 안에 있고 아직 DB 기록이 없는
  // 차량은 다음 프레임에서 다시 보정 저장합니다.
  const auto &activeVehicles = m_tracker.vehicles();
  for (auto it = activeVehicles.cbegin(); it != activeVehicles.cend(); ++it) {
    const VehicleState &vs = it.value();
    if (vs.occupiedRoiIndex >= 0 && !vs.notified &&
        vs.roiEntryMs > 0 && (nowMs - vs.roiEntryMs) >= kEntryPersistDelayMs) {
      handleNewEntry(vs);
    }
  }


  //타임아웃된 차량 정리 (출차 처리)
  const QList<VehicleState> departed =
      m_tracker.pruneStale(nowMs, pruneTimeoutMs);
  for (const VehicleState &vs : departed) {
    handleDeparture(vs);
  }
}

void ParkingService::updateReidFeatures(const QList<ObjectInfo> &objects) {
  qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_tracker.updateReidFeatures(objects, nowMs);
}

void ParkingService::processOcrResult(int objectId,
                                      const QString &plateNumber)
{
  m_tracker.setPlateNumber(objectId, plateNumber);

  const auto &vehicles = m_tracker.vehicles();
  if (!vehicles.contains(objectId)) {
    return;
  }

  const VehicleState &vs = vehicles[objectId];

  if (vs.occupiedRoiIndex >= 0 && !plateNumber.isEmpty()) {
    QString error;
    if (!m_repository.updateActivePlateByObjectId(m_cameraKey, objectId,
                                                  plateNumber, &error)) {
      handleNewEntry(vs);
    }
  }
}

QVector<QJsonObject> ParkingService::recentLogs(int limit) const 
{
  return m_repository.recentLogs(m_cameraKey, limit);
}

QVector<QJsonObject> ParkingService::searchByPlate(const QString &plate) const 
{
  return m_repository.searchByPlate(m_cameraKey, plate);
}

bool ParkingService::updatePlate(int recordId, const QString &newPlate) 
{
  return m_repository.updatePlate(m_cameraKey, recordId, newPlate);
}

bool ParkingService::deleteLog(int recordId, QString *errorMessage) 
{
  return m_repository.deleteLog(m_cameraKey, recordId, errorMessage);
}

bool ParkingService::updatePayment(const QString &plateNumber, int totalAmount,
                                   const QString &payStatus,
                                   QString *errorMessage)
{
  return m_repository.updatePayment(m_cameraKey, plateNumber, totalAmount,
                                    payStatus, errorMessage);
}

void ParkingService::forceObjectData(int objectId, const QString &type,
                                     const QString &plate, double score,
                                     const QRectF &bbox) 
{
  ObjectInfo info;
  info.id = objectId;
  info.type = type;
  info.plate = plate;
  info.score = (float)score;
  info.rect = bbox;

  // Tracker 상태 강제 업데이트
  m_tracker.forceTrackState(objectId, info);

  // 번호판 변경 시 OCR 처리 로직 트리거
  if (!plate.isEmpty()) {
    processOcrResult(objectId, plate);
  }
}

VehicleState ParkingService::getVehicleState(int objectId) const
{
  if (m_tracker.vehicles().contains(objectId)) {
    return m_tracker.vehicles()[objectId];
  }
  return VehicleState();
}

QList<VehicleState> ParkingService::activeVehicles() const 
{
  return m_tracker.vehicles().values();
}

void ParkingService::handleNewEntry(const VehicleState &vs) 
{
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (vs.roiEntryMs > 0 && (nowMs - vs.roiEntryMs) < kEntryPersistDelayMs) {
    return;
  }

  const QDateTime now = QDateTime::currentDateTime();
  const QString zoneName = zoneNameForIndex(vs.occupiedRoiIndex);
  const QString vehicleLabel =
      !vs.plateNumber.isEmpty() ? vs.plateNumber
                                : QStringLiteral("OBJ#%1").arg(vs.objectId);

  QJsonObject existing =
      m_repository.findActiveByObjectId(m_cameraKey, vs.objectId);
  if (!existing.isEmpty()) {
    if (existing["plate_number"].toString().trimmed().isEmpty() &&
        !vs.plateNumber.isEmpty()) {
      m_repository.updateActivePlateByObjectId(m_cameraKey, vs.objectId,
                                               vs.plateNumber);
    }
    emit logMessage(QString("[Parking] %1 — already has active entry, skipping")
                        .arg(vehicleLabel));
    m_tracker.markNotified(vs.objectId);
    return;
  }

  QDateTime entryTime = vs.roiEntryMs > 0 ? QDateTime::fromMSecsSinceEpoch(vs.roiEntryMs) : now;
  int recordId = m_repository.insertEntry(m_cameraKey, vs.objectId,
                                          vs.plateNumber, zoneName,
                                          vs.occupiedRoiIndex, entryTime);
  if (recordId >= 0) {
    emit logMessage(QString("[Parking] Entry recorded: %1 at %2 (DB ID: %3)")
                        .arg(vehicleLabel, zoneName)
                        .arg(recordId));
    emit vehicleEntered(vs.occupiedRoiIndex, vehicleLabel);
    m_tracker.markNotified(vs.objectId);
  }
}

void ParkingService::handleDeparture(const VehicleState &vs)
{
  const QDateTime now = QDateTime::currentDateTime();
  const QString vehicleLabel =
      !vs.plateNumber.isEmpty() ? vs.plateNumber
                                : QStringLiteral("OBJ#%1").arg(vs.objectId);

  // DB에서 활성 레코드 찾아 출차 시각 업데이트
  QJsonObject active =
      m_repository.findActiveByObjectId(m_cameraKey, vs.objectId);
  if (!active.isEmpty()) 
  {
    int recordId = active["id"].toInt();
    m_repository.updateExit(recordId, now);

    emit logMessage(QString("[Parking] Exit recorded: %1 from %2")
                        .arg(vehicleLabel, zoneNameForIndex(vs.occupiedRoiIndex)));
    emit vehicleDeparted(vs.occupiedRoiIndex, vehicleLabel);

    // TODO: 텔레그램 출차 알림 + 요금 정보 전송
  }
}

QString ParkingService::zoneNameForIndex(int roiIndex) const
{
  if (roiIndex >= 0 && roiIndex < m_roiZoneNames.size()) {
    const QString zoneName = m_roiZoneNames.at(roiIndex).trimmed();
    if (!zoneName.isEmpty()) {
      return zoneName;
    }
  }

  return roiIndex >= 0 ? QStringLiteral("ROI #%1").arg(roiIndex + 1)
                       : QStringLiteral("-");
}
