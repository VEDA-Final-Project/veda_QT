#include "parkinglogpanelcontroller.h"

#include "parking/parkingfeepolicy.h"
#include "parking/parkingservice.h"
#include <QDateTime>
#include <QLineEdit>
#include <QPushButton>
#include <QRectF>
#include <QSpinBox>
#include <Qt>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <algorithm>
#include <utility>

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

void populateParkingTable(QTableWidget *table, const QVector<QJsonObject> &logs) {
  if (!table) {
    return;
  }

  table->setRowCount(0);

  for (int i = 0; i < logs.size(); ++i) {
    const QJsonObject &row = logs[i];
    table->insertRow(i);
    const QString zoneName = row["zone_name"].toString().trimmed();
    QTableWidgetItem *idItem =
        new QTableWidgetItem(QString::number(row["id"].toInt()));
    idItem->setData(Qt::UserRole, row["camera_key"].toString());
    table->setItem(i, 0, idItem);
    table->setItem(
        i, 1, new QTableWidgetItem(QString::number(row["object_id"].toInt())));
    table->setItem(i, 2, new QTableWidgetItem(row["plate_number"].toString()));
    table->setItem(
        i, 3,
        new QTableWidgetItem(zoneName.isEmpty()
                                 ? QStringLiteral("ROI #%1")
                                       .arg(row["roi_index"].toInt() + 1)
                                 : zoneName));
    table->setItem(
        i, 4,
        new QTableWidgetItem(formatParkingDateTime(
            row["entry_time"].toString())));
    table->setItem(
        i, 5,
        new QTableWidgetItem(formatParkingDateTime(
            row["exit_time"].toString())));
    table->setItem(i, 6, new QTableWidgetItem(row["pay_status"].toString()));
    table->setItem(
        i, 7,
        new QTableWidgetItem(QString::number(calculateDisplayedParkingFee(row))));
  }
}
} // namespace

ParkingLogPanelController::ParkingLogPanelController(const UiRefs &uiRefs,
                                                     Context context,
                                                     QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void ParkingLogPanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnRefreshLogs) {
    connect(m_ui.btnRefreshLogs, &QPushButton::clicked, this,
            &ParkingLogPanelController::onRefreshParkingLogs);
  }
  if (m_ui.btnSearchPlate) {
    connect(m_ui.btnSearchPlate, &QPushButton::clicked, this,
            &ParkingLogPanelController::onSearchParkingLogs);
  }
  if (m_ui.btnForcePlate) {
    connect(m_ui.btnForcePlate, &QPushButton::clicked, this,
            &ParkingLogPanelController::onForcePlate);
  }
  if (m_ui.btnEditPlate) {
    connect(m_ui.btnEditPlate, &QPushButton::clicked, this,
            &ParkingLogPanelController::onEditPlate);
  }
}

void ParkingLogPanelController::onRefreshParkingLogs() {
  const QVector<ParkingService *> services =
      m_context.allParkingServicesProvider
          ? m_context.allParkingServicesProvider()
          : QVector<ParkingService *>();
  if (services.isEmpty()) {
    return;
  }

  const QVector<QJsonObject> logs = combinedRecentLogs(services, 100);
  populateParkingTable(m_ui.parkingLogTable, logs);
  appendLog(QString("[DB][All Channels] 전체 새로고침: %1건 표시")
                .arg(logs.size()));
}

void ParkingLogPanelController::onSearchParkingLogs() {
  if (!m_ui.plateSearchInput) {
    return;
  }

  const QString keyword = m_ui.plateSearchInput->text().trimmed();
  if (keyword.isEmpty()) {
    onRefreshParkingLogs();
    return;
  }

  const QVector<ParkingService *> services =
      m_context.allParkingServicesProvider
          ? m_context.allParkingServicesProvider()
          : QVector<ParkingService *>();
  if (services.isEmpty()) {
    return;
  }

  const QVector<QJsonObject> logs = combinedSearchLogs(services, keyword);
  populateParkingTable(m_ui.parkingLogTable, logs);
  appendLog(QString("[DB][All Channels] '%1' 검색 결과: %2건")
                .arg(keyword)
                .arg(logs.size()));
}

void ParkingLogPanelController::onForcePlate() {
  if (!m_ui.forceObjectIdInput || !m_ui.forcePlateInput) {
    return;
  }

  const int objectId = m_ui.forceObjectIdInput->value();
  const QString plate = m_ui.forcePlateInput->text().trimmed();

  const QString cameraKey =
      m_ui.btnForcePlate ? m_ui.btnForcePlate->property("cameraKey").toString()
                         : QString();
  ParkingService *service =
      (!cameraKey.isEmpty() && m_context.parkingServiceForCameraKeyProvider)
          ? m_context.parkingServiceForCameraKeyProvider(cameraKey)
          : (m_context.parkingServiceProvider
                 ? m_context.parkingServiceProvider()
                 : nullptr);
  if (!service) {
    return;
  }

  const VehicleState currentState = service->getVehicleState(objectId);
  const QString type =
      currentState.type.trimmed().isEmpty() ? QStringLiteral("Vehicle")
                                            : currentState.type.trimmed();
  const double score = currentState.score > 0.0 ? currentState.score : 1.0;
  const QRectF bbox = currentState.boundingBox;

  service->forceObjectData(objectId, type, plate, score, bbox);
  appendLog(QString("[DB] 강제 업데이트 요청: ID=%1").arg(objectId));
}

void ParkingLogPanelController::onEditPlate() {
  if (!m_ui.parkingLogTable || !m_ui.editPlateInput) {
    return;
  }

  const int currentRow = m_ui.parkingLogTable->currentRow();
  if (currentRow < 0) {
    appendLog("[DB] 수정할 레코드를 먼저 선택해주세요.");
    return;
  }

  const QString newPlate = m_ui.editPlateInput->text().trimmed();
  if (newPlate.isEmpty()) {
    appendLog("[DB] 새 번호판을 입력해주세요.");
    return;
  }

  QTableWidgetItem *idItem = m_ui.parkingLogTable->item(currentRow, 0);
  if (!idItem) {
    return;
  }

  const int recordId = idItem->text().toInt();
  const QString cameraKey = idItem->data(Qt::UserRole).toString();
  ParkingService *service =
      m_context.parkingServiceForCameraKeyProvider
          ? m_context.parkingServiceForCameraKeyProvider(cameraKey)
          : (m_context.parkingServiceProvider
                 ? m_context.parkingServiceProvider()
                 : nullptr);
  if (service && service->updatePlate(recordId, newPlate)) {
    appendLog(QString("[DB][%1] 번호판 수정 완료: ID=%2 → %3")
                  .arg(cameraKey, QString::number(recordId), newPlate));
    onRefreshParkingLogs();
    return;
  }
  appendLog(QString("[DB][%1] 번호판 수정 실패: ID=%2")
                .arg(cameraKey, QString::number(recordId)));
}

void ParkingLogPanelController::deleteParkingLog() {
  if (!m_ui.parkingLogTable) {
    return;
  }

  const int row = m_ui.parkingLogTable->currentRow();
  if (row < 0) {
    return;
  }

  QTableWidgetItem *idItem = m_ui.parkingLogTable->item(row, 0);
  if (!idItem) {
    return;
  }
  const int id = idItem->text().toInt();
  const QString cameraKey = idItem->data(Qt::UserRole).toString();

  ParkingService *service =
      m_context.parkingServiceForCameraKeyProvider
          ? m_context.parkingServiceForCameraKeyProvider(cameraKey)
          : (m_context.parkingServiceProvider
                 ? m_context.parkingServiceProvider()
                 : nullptr);
  QString error;
  if (service && service->deleteLog(id, &error)) {
    appendLog(QString("[DB][%1] 주차 기록 삭제 완료: ID=%2")
                  .arg(cameraKey)
                  .arg(id));
    onRefreshParkingLogs();
    return;
  }

  const QString reason = error.isEmpty() ? QStringLiteral("unknown") : error;
  appendLog(QString("[DB][%1] 주차 기록 삭제 실패: ID=%2 (%3)")
                .arg(cameraKey, QString::number(id), reason));
}

void ParkingLogPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
