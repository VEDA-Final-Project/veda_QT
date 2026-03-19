#include "vehicleinfopanelcontroller.h"

#include <QPushButton>
#include <QTableWidget>
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
  m_ui.vehicleTable->setRowCount(0);
}

void VehicleInfoPanelController::deleteVehicle() {
  appendLog(QString::fromUtf8("[DB] 차량 DB 기능은 제거되어 비활성화되었습니다."));
}

void VehicleInfoPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
