#include "parkinglogpanelcontroller.h"

#include "application/db/parking/parkinglogapplicationservice.h"
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <Qt>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <utility>

namespace {
void populateParkingTable(QTableWidget *table, const QVector<ParkingLogRow> &logs) {
  if (!table) {
    return;
  }

  table->setRowCount(0);

  for (int i = 0; i < logs.size(); ++i) {
    const ParkingLogRow &row = logs[i];
    table->insertRow(i);
    QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(row.id));
    idItem->setData(Qt::UserRole, row.cameraKey);
    table->setItem(i, 0, idItem);
    table->setItem(i, 1, new QTableWidgetItem(QString::number(row.objectId)));
    table->setItem(i, 2, new QTableWidgetItem(row.plateNumber));
    table->setItem(i, 3, new QTableWidgetItem(row.zoneName));
    table->setItem(i, 4, new QTableWidgetItem(row.entryTime));
    table->setItem(i, 5, new QTableWidgetItem(row.exitTime));
    table->setItem(i, 6, new QTableWidgetItem(row.payStatus));
    table->setItem(i, 7,
                   new QTableWidgetItem(QString::number(row.displayAmount)));
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
  if (!m_context.service) {
    return;
  }

  const QVector<ParkingLogRow> logs = m_context.service->getRecentLogs(100);
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

  if (!m_context.service) {
    return;
  }

  const QVector<ParkingLogRow> logs = m_context.service->searchLogs(keyword);
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
  if (!m_context.service) {
    return;
  }

  const OperationResult result =
      m_context.service->forcePlate(cameraKey, objectId, plate);
  appendLog(result.message);
  if (result.success && result.shouldRefresh) {
    if (m_ui.plateSearchInput &&
        !m_ui.plateSearchInput->text().trimmed().isEmpty()) {
      onSearchParkingLogs();
    } else {
      onRefreshParkingLogs();
    }
    if (m_context.refreshVehicleTable) {
      m_context.refreshVehicleTable();
    }
  }
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
  if (!m_context.service) {
    return;
  }

  const OperationResult result =
      m_context.service->updateLogPlate(cameraKey, recordId, newPlate);
  appendLog(result.message);
  if (result.success && result.shouldRefresh) {
    onRefreshParkingLogs();
  }
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
  if (!m_context.service) {
    return;
  }

  const OperationResult result = m_context.service->deleteLog(cameraKey, id);
  appendLog(result.message);
  if (result.success && result.shouldRefresh) {
    onRefreshParkingLogs();
  }
}

void ParkingLogPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
