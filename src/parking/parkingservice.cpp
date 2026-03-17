#include "parkingservice.h"
#include "../telegram/telegrambotapi.h"
#include "vehicletracker.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>


ParkingService::ParkingService(QObject *parent) : QObject(parent) {}

bool ParkingService::init(QString *errorMessage) {
  return m_repository.init(errorMessage);
}

void ParkingService::setTelegramApi(TelegramBotAPI *api) { m_telegram = api; }
void ParkingService::setCameraKey(const QString &cameraKey) {
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

QString ParkingService::cameraKey() const { return m_cameraKey; }

void ParkingService::updateRoiPolygons(const QList<QPolygonF> &polygons) {
  m_tracker.setRoiPolygons(polygons);
}

void ParkingService::processMetadata(const QList<ObjectInfo> &objects,
                                     int cropOffsetX, int effectiveWidth,
                                     int sourceHeight, qint64 pruneTimeoutMs) {
  qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
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

  // 3. 타임아웃된 차량 정리 (출차 처리)
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
