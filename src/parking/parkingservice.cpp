#include "parking/parkingservice.h"
#include "telegram/telegrambotapi.h"
#include <QDateTime>
#include <QDebug>

ParkingService::ParkingService(QObject *parent) : QObject(parent) {}

bool ParkingService::init(QString *errorMessage) {
  return m_repository.init(errorMessage);
}

void ParkingService::setTelegramApi(TelegramBotAPI *api) { m_telegram = api; }
void ParkingService::setCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  m_cameraKey = trimmed.isEmpty() ? QStringLiteral("camera") : trimmed;
}

QString ParkingService::cameraKey() const { return m_cameraKey; }

void ParkingService::updateRoiPolygons(const QList<QPolygonF> &polygons) {
  m_tracker.setRoiPolygons(polygons);
}

void ParkingService::processMetadata(const QList<ObjectInfo> &objects,
                                     int cropOffsetX, int effectiveWidth,
                                     int sourceHeight, qint64 pruneTimeoutMs) {
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  // 1. 차량 추적 업데이트 → 새로 ROI에 진입한 차량 감지
  const QList<VehicleState> newEntries = m_tracker.update(
      objects, cropOffsetX, effectiveWidth, sourceHeight, nowMs);

  // 2. 새로 진입한 차량 이벤트 로깅
  for (const VehicleState &vs : newEntries) {
    if (!vs.ocrRequested) {
      emit logMessage(QString("[Parking] Vehicle ID:%1 entered ROI #%2")
                          .arg(vs.objectId)
                          .arg(vs.occupiedRoiIndex + 1));
    }
  }

  // 3. 타임아웃된 차량 정리 (출차 처리)
  const QList<VehicleState> departed =
      m_tracker.pruneStale(nowMs, pruneTimeoutMs);
  for (const VehicleState &vs : departed) {
    handleDeparture(vs);
  }
}

void ParkingService::processOcrResult(int objectId,
                                      const QString &plateNumber) {
  m_tracker.setPlateNumber(objectId, plateNumber);

  const auto &vehicles = m_tracker.vehicles();
  if (!vehicles.contains(objectId)) {
    return;
  }

  const VehicleState &vs = vehicles[objectId];

  if (vs.occupiedRoiIndex >= 0 && !plateNumber.isEmpty()) {
    handleNewEntry(vs);
  }
}

QVector<QJsonObject> ParkingService::recentLogs(int limit) const {
  return m_repository.recentLogs(m_cameraKey, limit);
}

QVector<QJsonObject> ParkingService::searchByPlate(const QString &plate) const {
  return m_repository.searchByPlate(m_cameraKey, plate);
}

bool ParkingService::updatePlate(int recordId, const QString &newPlate) {
  return m_repository.updatePlate(m_cameraKey, recordId, newPlate);
}

bool ParkingService::deleteLog(int recordId, QString *errorMessage) {
  return m_repository.deleteLog(m_cameraKey, recordId, errorMessage);
}

void ParkingService::forceObjectData(int objectId, const QString &type,
                                     const QString &plate, double score,
                                     const QRectF &bbox) {
  emit logMessage(QString("[Parking] Force Object: ID=%1, Type=%2, Plate=%3")
                      .arg(objectId)
                      .arg(type, plate));

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

VehicleState ParkingService::getVehicleState(int objectId) const {
  if (m_tracker.vehicles().contains(objectId)) {
    return m_tracker.vehicles()[objectId];
  }
  return VehicleState();
}

QList<VehicleState> ParkingService::activeVehicles() const {
  return m_tracker.vehicles().values();
}

void ParkingService::handleNewEntry(const VehicleState &vs) {
  const QDateTime now = QDateTime::currentDateTime();

  // DB에 이미 활성 레코드가 있는지 확인 (중복 입차 방지)
  QJsonObject existing =
      m_repository.findActiveByPlate(m_cameraKey, vs.plateNumber);
  if (!existing.isEmpty()) {
    emit logMessage(QString("[Parking] %1 — already has active entry, skipping")
                        .arg(vs.plateNumber));
    return;
  }

  // DB에 입차 기록 생성
  QDateTime entryTime =
      vs.roiEntryMs > 0 ? QDateTime::fromMSecsSinceEpoch(vs.roiEntryMs) : now;
  int recordId = m_repository.insertEntry(m_cameraKey, vs.plateNumber,
                                          vs.occupiedRoiIndex, entryTime);
  if (recordId >= 0) {
    emit logMessage(
        QString("[Parking] Entry recorded: %1 at ROI #%2 (DB ID: %3)")
            .arg(vs.plateNumber)
            .arg(vs.occupiedRoiIndex + 1)
            .arg(recordId));
    emit vehicleEntered(vs.occupiedRoiIndex, vs.plateNumber);

    // TODO: 텔레그램 입차 알림 전송
    // if (m_telegram) {
    //   m_telegram->sendEntryNotice(vs.plateNumber, now);
    // }
  }

  m_tracker.markNotified(vs.objectId);
}

void ParkingService::handleDeparture(const VehicleState &vs) {
  if (vs.plateNumber.isEmpty()) {
    return; // 번호판 미인식 차량은 출차 기록 안 남김
  }

  const QDateTime now = QDateTime::currentDateTime();

  // DB에서 활성 레코드 찾아 출차 시각 업데이트
  QJsonObject active =
      m_repository.findActiveByPlate(m_cameraKey, vs.plateNumber);
  if (!active.isEmpty()) {
    int recordId = active["id"].toInt();
    m_repository.updateExit(recordId, now);

    emit logMessage(QString("[Parking] Exit recorded: %1 from ROI #%2")
                        .arg(vs.plateNumber)
                        .arg(vs.occupiedRoiIndex + 1));
    emit vehicleDeparted(vs.occupiedRoiIndex, vs.plateNumber);

    // TODO: 텔레그램 출차 알림 + 요금 정보 전송
    // if (m_telegram) {
    //   m_telegram->sendExitNotice(vs.plateNumber, now, fee);
    // }
  }
}
