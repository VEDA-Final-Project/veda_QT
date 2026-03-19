#include "parking/parkingservice.h"
#include "telegram/telegrambotapi.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

namespace {
constexpr qint64 kEntryPersistDelayMs = 5000;

bool isPendingPlateText(const QString &plateNumber)
{
  const QString trimmed = plateNumber.trimmed();
  return trimmed.isEmpty() || trimmed.contains(QStringLiteral("인식중"));
}

QString resolvedPlateText(const QString &plateNumber)
{
  return isPendingPlateText(plateNumber) ? QString() : plateNumber.trimmed();
}
}

ParkingService::ParkingService(QObject *parent) : QObject(parent) {}

void ParkingService::processOcrStarted(int objectId)
{
  const auto &vehicles = m_tracker.vehicles();
  if (vehicles.contains(objectId)) {
    const VehicleState &vs = vehicles[objectId];
    if (!resolvedPlateText(vs.plateNumber).isEmpty()) {
      return;
    }
    if (!vs.reidId.isEmpty() && vs.reidId != QStringLiteral("V---")) {
      m_ocrObjectReidSnapshot.insert(objectId, vs.reidId);
    }
  }

  m_tracker.setPlateNumber(objectId, QStringLiteral("인식중.."));
}


bool ParkingService::init(QString *errorMessage)
{
  if (!m_repository.init(errorMessage)) {
    return false;
  }
  return true;
}

void ParkingService::setTelegramApi(TelegramBotAPI *api)
{
  m_telegram = api;
}

void ParkingService::setCameraKey(const QString &cameraKey) 
{
  const QString trimmed = cameraKey.trimmed();
  m_cameraKey = trimmed.isEmpty() ? QStringLiteral("camera") : trimmed;

  const QRegularExpression re(QStringLiteral("(\\d+)"));
  const QRegularExpressionMatch match = re.match(m_cameraKey);
  QString prefix;
  if (match.hasMatch()) {
    prefix = QStringLiteral("C") + match.captured(1);
  } else if (m_cameraKey.toLower().contains(QStringLiteral("camera"))) {
    prefix = QStringLiteral("C1");
  } else {
    prefix = m_cameraKey.left(1).toUpper();
    if (prefix.isEmpty()) {
      prefix = QStringLiteral("C1");
    }
  }
  m_tracker.setIdPrefix(prefix);
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
                                     int sourceHeight, qint64 pruneTimeoutMs) 
{
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  // 1. 차량 추적 업데이트 → 새로 ROI에 진입한 차량 감지
  QList<VehicleState> roiDepartures;
  const QList<VehicleState> newEntries = m_tracker.update(
      objects, cropOffsetX, effectiveWidth, sourceHeight, nowMs,
      &roiDepartures);

  for (const VehicleState &vs : newEntries) {
    syncActivePlateIfNeeded(vs);
    if (vs.occupiedRoiIndex >= 0 && !vs.notified &&
        vs.roiEntryMs > 0 && (nowMs - vs.roiEntryMs) >= kEntryPersistDelayMs) {
      handleNewEntry(vs);
    }
  }

  for (const VehicleState &vs : roiDepartures) {
    handleDeparture(vs);
  }

  // 새 진입 이벤트를 놓쳤더라도, 현재 ROI 안에 있고 아직 DB 기록이 없는
  // 차량은 다음 프레임에서 다시 보정 저장합니다.
  const auto &activeVehicles = m_tracker.vehicles();
  for (auto it = activeVehicles.cbegin(); it != activeVehicles.cend(); ++it) {
    const VehicleState &vs = it.value();
    syncActivePlateIfNeeded(vs);
    if (vs.occupiedRoiIndex >= 0 && !vs.notified &&
        vs.roiEntryMs > 0 && (nowMs - vs.roiEntryMs) >= kEntryPersistDelayMs) {
      handleNewEntry(vs);
    }
  }

  pruneStaleVehicles(pruneTimeoutMs);
}

bool ParkingService::pruneStaleVehicles(qint64 timeoutMs)
{
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const QList<VehicleState> departed = m_tracker.pruneStale(nowMs, timeoutMs);
  for (const VehicleState &vs : departed) {
    handleDeparture(vs);
  }
  return !departed.isEmpty();
}

void ParkingService::updateReidFeatures(const QList<ObjectInfo> &objects)
{
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_tracker.updateReidFeatures(objects, nowMs);

  const auto &vehicles = m_tracker.vehicles();
  for (auto it = vehicles.cbegin(); it != vehicles.cend(); ++it) {
    const int objectId = it.key();
    const VehicleState &vs = it.value();
    if (vs.reidId.isEmpty() || vs.reidId == QStringLiteral("V---")) {
      continue;
    }

    const QString previous = m_lastReidByObjectId.value(objectId);
    if (previous == vs.reidId) {
      continue;
    }

    QString error;
    if (m_repository.updateActiveReidByObjectId(m_cameraKey, objectId, vs.reidId,
                                                &error)) {
      m_lastReidByObjectId.insert(objectId, vs.reidId);
    }

  }
}

void ParkingService::processOcrResult(int objectId,
                                      const QString &plateNumber)
{
  m_tracker.setPlateNumber(objectId, plateNumber);
  const QString resolvedPlate = resolvedPlateText(plateNumber);

  const auto &vehicles = m_tracker.vehicles();
  QString reidId;
  VehicleState vs;
  bool hasVehicle = false;
  if (vehicles.contains(objectId)) {
    vs = vehicles[objectId];
    hasVehicle = true;
    if (!vs.reidId.isEmpty() && vs.reidId != QStringLiteral("V---")) {
      reidId = vs.reidId;
    }
  }
  if (reidId.isEmpty()) {
    reidId = m_ocrObjectReidSnapshot.value(objectId);
  }
  if (!reidId.isEmpty()) {
    m_tracker.setPlateNumberForReid(reidId, plateNumber);
  }
  m_ocrObjectReidSnapshot.remove(objectId);

  bool insertedNewEntry = false;
  bool updatedActiveEntry = false;
  QString previousActivePlate;
  if (hasVehicle && vs.occupiedRoiIndex >= 0 && !resolvedPlate.isEmpty()) {
    const QJsonObject activeBefore =
        m_repository.findActiveByObjectId(m_cameraKey, objectId);
    previousActivePlate =
        resolvedPlateText(activeBefore["plate_number"].toString());

    QString error;
    bool updated = false;
    if (!reidId.isEmpty()) {
      updated = m_repository.updateActivePlateByReidId(m_cameraKey, reidId,
                                                       plateNumber, &error);
    }
    if (!updated) {
      updated = m_repository.updateActivePlateByObjectId(m_cameraKey, objectId,
                                                         plateNumber, &error);
    }

    if (updated) {
      updatedActiveEntry = true;
    } else {
      handleNewEntry(vs);
      insertedNewEntry = true;
    }
  }

  if (m_telegram && hasVehicle && vs.occupiedRoiIndex >= 0 &&
      !resolvedPlate.isEmpty() && !insertedNewEntry &&
      updatedActiveEntry && previousActivePlate != resolvedPlate) {
    m_telegram->sendEntryNotice(resolvedPlate);
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
  const QString resolvedPlate = resolvedPlateText(vs.plateNumber);
  const QString vehicleLabel =
      !resolvedPlate.isEmpty() ? resolvedPlate
                               : QStringLiteral("OBJ#%1").arg(vs.objectId);

  QJsonObject existing =
      m_repository.findActiveByObjectId(m_cameraKey, vs.objectId);
  if (!existing.isEmpty()) {
    if (resolvedPlateText(existing["plate_number"].toString()).isEmpty()) {
      if (!vs.reidId.isEmpty() && vs.reidId != QStringLiteral("V---") &&
          !resolvedPlate.isEmpty()) {
        m_repository.updateActivePlateByReidId(m_cameraKey, vs.reidId,
                                               resolvedPlate);
      } else if (!resolvedPlate.isEmpty()) {
        m_repository.updateActivePlateByObjectId(m_cameraKey, vs.objectId,
                                                 resolvedPlate);
      }
    }
    emit logMessage(QString("[Parking] %1 — already has active entry, skipping")
                        .arg(vehicleLabel));
    m_tracker.markNotified(vs.objectId);
    return;
  }

  QDateTime entryTime = vs.roiEntryMs > 0 ? QDateTime::fromMSecsSinceEpoch(vs.roiEntryMs) : now;
  int recordId = m_repository.insertEntry(m_cameraKey, vs.objectId,
                                          resolvedPlate, zoneName,
                                          vs.occupiedRoiIndex, vs.reidId,
                                          entryTime);
  if (recordId >= 0) {
    emit logMessage(QString("[Parking] Entry recorded: %1 at %2 (DB ID: %3)")
                        .arg(vehicleLabel, zoneName)
                        .arg(recordId));
    emit vehicleEntered(vs.occupiedRoiIndex, vehicleLabel);
    if (m_telegram && !resolvedPlate.isEmpty()) {
      m_telegram->sendEntryNotice(resolvedPlate);
    }
    m_tracker.markNotified(vs.objectId);
  }
}

void ParkingService::handleDeparture(const VehicleState &vs)
{
  const QDateTime now = QDateTime::currentDateTime();
  const QString resolvedPlate = resolvedPlateText(vs.plateNumber);
  const QString vehicleLabel =
      !resolvedPlate.isEmpty() ? resolvedPlate
                               : QStringLiteral("OBJ#%1").arg(vs.objectId);

  // DB에서 활성 레코드 찾아 출차 시각 업데이트
  QJsonObject active =
      m_repository.findActiveByObjectId(m_cameraKey, vs.objectId);
  if (!active.isEmpty()) 
  {
    int recordId = active["id"].toInt();
    int totalAmount = active["total_amount"].toInt();
    if (!m_repository.updateExit(recordId, now, &totalAmount)) {
      emit logMessage(QString("[Parking] Exit update failed: %1")
                          .arg(vehicleLabel));
      return;
    }

    emit logMessage(QString("[Parking] Exit recorded: %1 from %2")
                        .arg(vehicleLabel, zoneNameForIndex(vs.occupiedRoiIndex)));
    emit vehicleDeparted(vs.occupiedRoiIndex, vehicleLabel);
    if (m_telegram && !resolvedPlate.isEmpty()) {
      m_telegram->sendExitNotice(resolvedPlate, totalAmount);
    }
  }
}

void ParkingService::syncActivePlateIfNeeded(const VehicleState &vs)
{
  const QString resolvedPlate = resolvedPlateText(vs.plateNumber);
  if (vs.objectId < 0 || resolvedPlate.isEmpty()) {
    return;
  }

  QJsonObject active = m_repository.findActiveByObjectId(m_cameraKey, vs.objectId);
  if (active.isEmpty()) {
    return;
  }

  if (!resolvedPlateText(active["plate_number"].toString()).isEmpty()) {
    return;
  }

  if (!vs.reidId.isEmpty() && vs.reidId != QStringLiteral("V---")) {
    if (m_repository.updateActivePlateByReidId(m_cameraKey, vs.reidId,
                                               resolvedPlate)) {
      return;
    }
  }

  m_repository.updateActivePlateByObjectId(m_cameraKey, vs.objectId, resolvedPlate);
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
