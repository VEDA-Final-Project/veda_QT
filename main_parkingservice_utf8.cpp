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

void ParkingService::updateRoiPolygons(const QList<QPolygon> &polygons) {
  m_tracker.setRoiPolygons(polygons);
}

void ParkingService::processMetadata(const QList<ObjectInfo> &objects,
                                     int frameWidth, int frameHeight,
                                     qint64 pruneTimeoutMs) {
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  // 1. 李⑤웾 異붿쟻 ?낅뜲?댄듃 ???덈줈 ROI??吏꾩엯??李⑤웾 媛먯?
  QList<VehicleState> newEntries =
      m_tracker.update(objects, frameWidth, frameHeight, nowMs);

  // 2. ?덈줈 吏꾩엯??李⑤웾 ?대깽??濡쒓퉭
  for (const VehicleState &vs : newEntries) {
    if (!vs.ocrRequested) {
      emit logMessage(
          QString("[Parking] Vehicle ID:%1 entered ROI #%2")
              .arg(vs.objectId)
              .arg(vs.occupiedRoiIndex + 1));
    }
  }

  // 3. ??꾩븘?껊맂 李⑤웾 ?뺣━ (異쒖감 泥섎━)
  QList<VehicleState> departed = m_tracker.pruneStale(nowMs, pruneTimeoutMs);
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
                      .arg(type)
                      .arg(plate));

  ObjectInfo info;
  info.id = objectId;
  info.type = type;
  info.plate = plate;
  info.score = (float)score;
  info.rect = bbox;

  // Tracker ?곹깭 媛뺤젣 ?낅뜲?댄듃
  m_tracker.forceTrackState(objectId, info);

  // 踰덊샇??蹂寃???OCR 泥섎━ 濡쒖쭅 ?몃━嫄?  if (!plate.isEmpty()) {
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

  // DB???대? ?쒖꽦 ?덉퐫?쒓? ?덈뒗吏 ?뺤씤 (以묐났 ?낆감 諛⑹?)
  QJsonObject existing =
      m_repository.findActiveByPlate(m_cameraKey, vs.plateNumber);
  if (!existing.isEmpty()) {
    emit logMessage(QString("[Parking] %1 ??already has active entry, skipping")
                        .arg(vs.plateNumber));
    return;
  }

  // DB???낆감 湲곕줉 ?앹꽦
  int recordId = m_repository.insertEntry(m_cameraKey, vs.plateNumber,
                                          vs.occupiedRoiIndex, now);
  if (recordId >= 0) {
    emit logMessage(
        QString("[Parking] Entry recorded: %1 at ROI #%2 (DB ID: %3)")
            .arg(vs.plateNumber)
            .arg(vs.occupiedRoiIndex + 1)
            .arg(recordId));
    emit vehicleEntered(vs.occupiedRoiIndex, vs.plateNumber);

    // TODO: ?붾젅洹몃옩 ?낆감 ?뚮┝ ?꾩넚
    // if (m_telegram) {
    //   m_telegram->sendEntryNotice(vs.plateNumber, now);
    // }
  }

  m_tracker.markNotified(vs.objectId);
}

void ParkingService::handleDeparture(const VehicleState &vs) {
  if (vs.plateNumber.isEmpty()) {
    return; // 踰덊샇??誘몄씤??李⑤웾? 異쒖감 湲곕줉 ???④?
  }

  const QDateTime now = QDateTime::currentDateTime();

  // DB?먯꽌 ?쒖꽦 ?덉퐫??李얠븘 異쒖감 ?쒓컖 ?낅뜲?댄듃
  QJsonObject active =
      m_repository.findActiveByPlate(m_cameraKey, vs.plateNumber);
  if (!active.isEmpty()) {
    int recordId = active["id"].toInt();
    m_repository.updateExit(recordId, now);

    emit logMessage(QString("[Parking] Exit recorded: %1 from ROI #%2")
                        .arg(vs.plateNumber)
                        .arg(vs.occupiedRoiIndex + 1));
    emit vehicleDeparted(vs.occupiedRoiIndex, vs.plateNumber);

    // TODO: ?붾젅洹몃옩 異쒖감 ?뚮┝ + ?붽툑 ?뺣낫 ?꾩넚
    // if (m_telegram) {
    //   m_telegram->sendExitNotice(vs.plateNumber, now, fee);
    // }
  }
}
