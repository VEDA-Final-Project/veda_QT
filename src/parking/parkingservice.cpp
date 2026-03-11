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

void ParkingService::setRoiNames(const QStringList &names) {
  m_roiNames = names;
}

void ParkingService::processMetadata(const QList<ObjectInfo> &objects,
                                     int cropOffsetX, int effectiveWidth,
                                     int sourceHeight, qint64 pruneTimeoutMs) {
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  // 1. 차량 추적 업데이트 → 새로 ROI에 진입한 차량 감지
  const QList<VehicleState> newEntries = m_tracker.update(
      objects, cropOffsetX, effectiveWidth, sourceHeight, nowMs);

  // 2. 새로 진입한 차량 이벤트 로깅 및 DB 등록
  for (const VehicleState &vs : newEntries) {
    if (!vs.ocrRequested) {
      emit logMessage(QString("[Parking] Vehicle ID:%1 entered ROI #%2")
                          .arg(vs.objectId)
                          .arg(vs.occupiedRoiIndex + 1));
      handleNewEntry(vs); // 번호판이 없어도 일단 DB에 세션 생성
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
  int vehicleId = m_tracker.setPlateNumber(objectId, plateNumber);

  const auto &vehicles = m_tracker.vehicles();
  if (!vehicles.contains(vehicleId)) {
    return;
  }

  const VehicleState &vs = vehicles[vehicleId];

  if (vs.occupiedRoiIndex >= 0 && !plateNumber.isEmpty()) {
    qDebug() << "[Parking] 번호판 인식 | ID:" << vehicleId << "->"
             << plateNumber;
    
    // 이미 DB에 이 ID로 들어온 기록이 있는지 확인
    QJsonObject active = m_repository.findActiveByObjectId(m_cameraKey, vehicleId);
    if (!active.isEmpty()) {
        // 있으면 번호판 정보만 업데이트
        int recordId = active["id"].toInt();
        m_repository.updatePlate(m_cameraKey, recordId, plateNumber);
        emit parkingLogsUpdated();
    } else {
        // 없으면 새로 생성 (handleNewEntry 내부에서 중복 체크함)
        handleNewEntry(vs);
    }
  }
}

QVector<QJsonObject> ParkingService::recentLogs(int limit) const {
  return m_repository.recentLogs(m_cameraKey, limit);
}

QVector<QJsonObject> ParkingService::searchByPlate(const QString &plate) const {
  return m_repository.searchByPlate(m_cameraKey, plate);
}

bool ParkingService::updatePlate(int recordId, const QString &newPlate) {
  if (m_repository.updatePlate(m_cameraKey, recordId, newPlate)) {
    emit parkingLogsUpdated();
    return true;
  }
  return false;
}

bool ParkingService::deleteLog(int recordId, QString *errorMessage) {
  if (m_repository.deleteLog(m_cameraKey, recordId, errorMessage)) {
    emit parkingLogsUpdated();
    return true;
  }
  return false;
}

bool ParkingService::manualInsert(const QString &plate, const QString &roiIndex, QString *errorMessage) {
  const QDateTime now = QDateTime::currentDateTime();
  // ROI 인덱스가 숫자인 경우 이름을 찾아보고, 아니면 그대로 사용
  bool ok = false;
  int idx = roiIndex.toInt(&ok);
  QString zoneName = roiIndex;
  if (ok && idx >= 0 && idx < m_roiNames.size()) {
      zoneName = m_roiNames[idx];
  }

  int recordId = m_repository.insertEntry(m_cameraKey, plate, now, -1, zoneName, errorMessage);
  if (recordId >= 0) {
      emit parkingLogsUpdated();
      return true;
  }
  return false;
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

  // DB에 이미 활성 레코드가 있는지 확인 (중복 입차 방지 - ID 기준)
  QJsonObject existing =
      m_repository.findActiveByObjectId(m_cameraKey, vs.objectId);
  if (!existing.isEmpty()) {
    return;
  }

  // DB에 입차 기록 생성
  QDateTime entryTime =
      vs.roiEntryMs > 0 ? QDateTime::fromMSecsSinceEpoch(vs.roiEntryMs) : now;

  // ROI 이름 조회
  const QString roiName =
      (vs.occupiedRoiIndex >= 0 && vs.occupiedRoiIndex < m_roiNames.size())
          ? m_roiNames[vs.occupiedRoiIndex]
          : QString();

  int recordId = m_repository.insertEntry(m_cameraKey, vs.plateNumber,
                                          entryTime, vs.objectId, roiName);

  qDebug() << "[Parking] 입차 감지 | ID:" << vs.objectId
           << "| 번호판:" << vs.plateNumber << "| 구역:" << roiName;
  if (recordId >= 0) {
    emit logMessage(
        QString("[Parking] Entry recorded: %1 at ROI #%2 (DB ID: %3)")
            .arg(vs.plateNumber)
            .arg(vs.occupiedRoiIndex + 1)
            .arg(recordId));
    emit vehicleEntered(vs.occupiedRoiIndex, vs.plateNumber);
    emit parkingLogsUpdated();

    // TODO: 텔레그램 입차 알림 전송
    // if (m_telegram) {
    //   m_telegram->sendEntryNotice(vs.plateNumber, now);
    // }
  }

  m_tracker.markNotified(vs.objectId);
}

void ParkingService::handleDeparture(const VehicleState &vs) {
  const QDateTime now = QDateTime::currentDateTime();

  // DB에서 활성 레코드 찾아 출차 시각 업데이트 (ID 기준)
  QJsonObject active =
      m_repository.findActiveByObjectId(m_cameraKey, vs.objectId);
  if (!active.isEmpty()) {
    int recordId = active["id"].toInt();
    m_repository.updateExit(recordId, now);

    qDebug() << "[Parking] 🔴 출차 감지 | ID:" << vs.objectId
             << "| 번호판:" << vs.plateNumber
             << "| 구역:" << active["zone_name"].toString();

    emit logMessage(QString("[Parking] Exit recorded: %1 from ROI #%2")
                        .arg(vs.plateNumber)
                        .arg(vs.occupiedRoiIndex + 1));
    emit vehicleDeparted(vs.occupiedRoiIndex, vs.plateNumber);
    emit parkingLogsUpdated();

    // TODO: 텔레그램 출차 알림 + 요금 정보 전송
    // if (m_telegram) {
    //   m_telegram->sendExitNotice(vs.plateNumber, now, fee);
    // }
  }
}
