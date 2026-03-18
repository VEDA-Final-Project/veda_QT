#include "vehicleinfopanelcontroller.h"

#include "database/vehiclerepository.h"
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <utility>

VehicleInfoPanelController::VehicleInfoPanelController(const UiRefs &uiRefs,
                                                       Context context,
                                                       QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void VehicleInfoPanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnRefreshVehicles) {
    connect(m_ui.btnRefreshVehicles, &QPushButton::clicked, this,
            &VehicleInfoPanelController::refreshVehicleTable);
  }
  if (m_ui.btnDeleteVehicle) {
    connect(m_ui.btnDeleteVehicle, &QPushButton::clicked, this,
            &VehicleInfoPanelController::deleteVehicle);
  }
}

void VehicleInfoPanelController::refreshVehicleTable() {
  if (!m_ui.vehicleTable) {
    return;
  }

  VehicleRepository repo;
  QString error;
  const QVector<QJsonObject> vehicles = repo.getAllVehicles(&error);

  m_ui.vehicleTable->setRowCount(0);
  for (int i = 0; i < vehicles.size(); ++i) {
    const QJsonObject &row = vehicles[i];
    m_ui.vehicleTable->insertRow(i);
    m_ui.vehicleTable->setItem(
        i, 0, new QTableWidgetItem(row["plate_number"].toString()));
    m_ui.vehicleTable->setItem(
        i, 1, new QTableWidgetItem(row["reid_id"].toString()));
    m_ui.vehicleTable->setItem(
        i, 2, new QTableWidgetItem(row["car_type"].toString()));
    m_ui.vehicleTable->setItem(
        i, 3, new QTableWidgetItem(row["car_color"].toString()));
    m_ui.vehicleTable->setItem(
        i, 4, new QTableWidgetItem(row["is_assigned"].toBool() ? "Yes" : "No"));
    m_ui.vehicleTable->setItem(
        i, 5, new QTableWidgetItem(row["updated_at"].toString()));
  }
}

void VehicleInfoPanelController::deleteVehicle() {
  if (!m_ui.vehicleTable) {
    return;
  }

  const int row = m_ui.vehicleTable->currentRow();
  if (row < 0) {
    return;
  }

  QTableWidgetItem *plateItem = m_ui.vehicleTable->item(row, 0);
  if (!plateItem) {
    return;
  }

  const QString plate = plateItem->text();

  VehicleRepository repo;
  QString error;
  if (repo.deleteVehicle(plate, &error)) {
    appendLog(QString("[DB] 차량 정보 삭제 완료: %1").arg(plate));
    refreshVehicleTable();
    return;
  }

  appendLog(QString("[DB] 차량 정보 삭제 실패: %1").arg(error));
}

void VehicleInfoPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
