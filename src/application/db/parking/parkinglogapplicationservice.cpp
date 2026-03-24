#include "application/db/parking/parkinglogapplicationservice.h"

#include "application/parking/parkingservice.h"
#include "domain/parking/parkingfeepolicy.h"
#include <QDateTime>
#include <QJsonObject>
#include <QRectF>
#include <Qt>
#include <algorithm>

namespace {
QDateTime parseParkingDateTime(const QString &rawIsoText) {
  QDateTime dt = QDateTime::fromString(rawIsoText, Qt::ISODateWithMs);
  if (!dt.isValid()) {
    dt = QDateTime::fromString(rawIsoText, Qt::ISODate);
  }
  return dt;
}

QString formatParkingDateTime(const QString &rawIsoText) {
  QDateTime dt = parseParkingDateTime(rawIsoText);
  if (!dt.isValid()) {
    return rawIsoText;
  }

  return dt.toLocalTime().toString(QStringLiteral("yyyy/MM/dd HH:mm"));
}

int calculateDisplayedParkingFee(const QJsonObject &row) {
  const QString exitTimeText = row["exit_time"].toString().trimmed();
  if (!exitTimeText.isEmpty()) {
    return row["total_amount"].toInt();
  }

  const QDateTime entryTime = parseParkingDateTime(row["entry_time"].toString());
  const QDateTime now = QDateTime::currentDateTime();
  if (!entryTime.isValid() || now <= entryTime) {
    return row["total_amount"].toInt();
  }
  return parking::calculateParkingFee(entryTime, now).totalAmount;
}

QVector<QJsonObject> combinedRecentLogs(const QVector<ParkingService *> &services,
                                        int limitPerService) {
  QVector<QJsonObject> logs;
  for (ParkingService *service : services) {
    if (!service) {
      continue;
    }
    const QVector<QJsonObject> serviceLogs = service->recentLogs(limitPerService);
    for (const QJsonObject &row : serviceLogs) {
      logs.append(row);
    }
  }

  std::sort(logs.begin(), logs.end(),
            [](const QJsonObject &a, const QJsonObject &b) {
              return parseParkingDateTime(a["entry_time"].toString()) >
                     parseParkingDateTime(b["entry_time"].toString());
            });
  return logs;
}

QVector<QJsonObject> combinedSearchLogs(const QVector<ParkingService *> &services,
                                        const QString &keyword) {
  QVector<QJsonObject> logs;
  for (ParkingService *service : services) {
    if (!service) {
      continue;
    }
    const QVector<QJsonObject> serviceLogs = service->searchByPlate(keyword);
    for (const QJsonObject &row : serviceLogs) {
      logs.append(row);
    }
  }

  std::sort(logs.begin(), logs.end(),
            [](const QJsonObject &a, const QJsonObject &b) {
              return parseParkingDateTime(a["entry_time"].toString()) >
                     parseParkingDateTime(b["entry_time"].toString());
            });
  return logs;
}
} // namespace

ParkingLogApplicationService::ParkingLogApplicationService(const Context &context,
                                                           QObject *parent)
    : QObject(parent), m_context(context) {}

QVector<ParkingLogRow>
ParkingLogApplicationService::getRecentLogs(int limitPerService) const {
  const QVector<ParkingService *> services =
      m_context.allParkingServicesProvider
          ? m_context.allParkingServicesProvider()
          : QVector<ParkingService *>();
  return toRows(combinedRecentLogs(services, limitPerService));
}

QVector<ParkingLogRow>
ParkingLogApplicationService::searchLogs(const QString &plateKeyword) const {
  const QString keyword = plateKeyword.trimmed();
  if (keyword.isEmpty()) {
    return getRecentLogs();
  }

  const QVector<ParkingService *> services =
      m_context.allParkingServicesProvider
          ? m_context.allParkingServicesProvider()
          : QVector<ParkingService *>();
  return toRows(combinedSearchLogs(services, keyword));
}

OperationResult ParkingLogApplicationService::forcePlate(const QString &cameraKey,
                                                         int objectId,
                                                         const QString &plate) const {
  if (plate.trimmed().isEmpty()) {
    return {false, QStringLiteral("[DB] 강제 업데이트 실패: 번호판을 입력해주세요."),
            false};
  }

  ParkingService *service = resolveService(cameraKey);
  if (!service) {
    return {false,
            QStringLiteral("[DB] 강제 업데이트 실패: 대상 카메라를 찾을 수 없습니다."),
            false};
  }

  const VehicleState currentState = service->getVehicleState(objectId);
  const QString type =
      currentState.type.trimmed().isEmpty() ? QStringLiteral("Vehicle")
                                            : currentState.type.trimmed();
  const double score = currentState.score > 0.0 ? currentState.score : 1.0;
  const QRectF bbox = currentState.boundingBox;

  service->forceObjectData(objectId, type, plate.trimmed(), score, bbox);
  return {true, QString("[DB] 강제 업데이트 요청: ID=%1").arg(objectId), true};
}

OperationResult ParkingLogApplicationService::updateLogPlate(
    const QString &cameraKey, int recordId, const QString &newPlate) const {
  const QString normalizedPlate = newPlate.trimmed();
  if (normalizedPlate.isEmpty()) {
    return {false, QStringLiteral("[DB] 새 번호판을 입력해주세요."), false};
  }

  ParkingService *service = resolveService(cameraKey);
  if (!service) {
    return {false,
            QString("[DB][%1] 번호판 수정 실패: 대상 카메라를 찾을 수 없습니다.")
                .arg(cameraKey),
            false};
  }

  if (service->updatePlate(recordId, normalizedPlate)) {
    return {true,
            QString("[DB][%1] 번호판 수정 완료: ID=%2 → %3")
                .arg(cameraKey, QString::number(recordId), normalizedPlate),
            true};
  }

  return {false,
          QString("[DB][%1] 번호판 수정 실패: ID=%2")
              .arg(cameraKey, QString::number(recordId)),
          false};
}

OperationResult ParkingLogApplicationService::deleteLog(
    const QString &cameraKey, int recordId) const {
  ParkingService *service = resolveService(cameraKey);
  if (!service) {
    return {false,
            QString("[DB][%1] 주차 기록 삭제 실패: 대상 카메라를 찾을 수 없습니다.")
                .arg(cameraKey),
            false};
  }

  QString error;
  if (service->deleteLog(recordId, &error)) {
    return {true,
            QString("[DB][%1] 주차 기록 삭제 완료: ID=%2")
                .arg(cameraKey)
                .arg(recordId),
            true};
  }

  const QString reason = error.isEmpty() ? QStringLiteral("unknown") : error;
  return {false,
          QString("[DB][%1] 주차 기록 삭제 실패: ID=%2 (%3)")
              .arg(cameraKey, QString::number(recordId), reason),
          false};
}

ParkingService *
ParkingLogApplicationService::resolveService(const QString &cameraKey) const {
  if (!cameraKey.trimmed().isEmpty() &&
      m_context.parkingServiceForCameraKeyProvider) {
    if (ParkingService *service =
            m_context.parkingServiceForCameraKeyProvider(cameraKey.trimmed())) {
      return service;
    }
  }

  return m_context.parkingServiceProvider ? m_context.parkingServiceProvider()
                                          : nullptr;
}

QVector<ParkingLogRow>
ParkingLogApplicationService::toRows(const QVector<QJsonObject> &logs) const {
  QVector<ParkingLogRow> rows;
  rows.reserve(logs.size());

  for (const QJsonObject &row : logs) {
    const QString zoneName = row["zone_name"].toString().trimmed();
    rows.append(ParkingLogRow{
        row["id"].toInt(),
        row["camera_key"].toString(),
        row["object_id"].toInt(),
        row["plate_number"].toString(),
        zoneName.isEmpty()
            ? QStringLiteral("ROI #%1").arg(row["roi_index"].toInt() + 1)
            : zoneName,
        formatParkingDateTime(row["entry_time"].toString()),
        formatParkingDateTime(row["exit_time"].toString()),
        row["pay_status"].toString(),
        calculateDisplayedParkingFee(row),
    });
  }

  return rows;
}
