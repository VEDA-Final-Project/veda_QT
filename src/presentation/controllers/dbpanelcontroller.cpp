#include "dbpanelcontroller.h"

#include "application/db/parking/parkinglogapplicationservice.h"
#include "application/db/user/useradminapplicationservice.h"
#include "application/db/zone/zonequeryapplicationservice.h"
#include "parkinglogpanelcontroller.h"
#include "userdbpanelcontroller.h"
#include "zonepanelcontroller.h"
#include <utility>

DbPanelController::DbPanelController(const UiRefs &uiRefs, Context context,
                                     QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {
  ParkingLogPanelController::UiRefs parkingUiRefs;
  parkingUiRefs.parkingLogTable = m_ui.parkingLogTable;
  parkingUiRefs.plateSearchInput = m_ui.plateSearchInput;
  parkingUiRefs.btnSearchPlate = m_ui.btnSearchPlate;
  parkingUiRefs.btnRefreshLogs = m_ui.btnRefreshLogs;
  parkingUiRefs.forcePlateInput = m_ui.forcePlateInput;
  parkingUiRefs.forceObjectIdInput = m_ui.forceObjectIdInput;
  parkingUiRefs.btnForcePlate = m_ui.btnForcePlate;
  parkingUiRefs.editPlateInput = m_ui.editPlateInput;
  parkingUiRefs.btnEditPlate = m_ui.btnEditPlate;

  ParkingLogPanelController::Context parkingContext;
  parkingContext.service = m_context.parkingLogService;
  parkingContext.logMessage = m_context.logMessage;

  m_parkingLogPanelController =
      new ParkingLogPanelController(parkingUiRefs, parkingContext, this);

  UserDbPanelController::UiRefs userUiRefs;
  userUiRefs.userDbTable = m_ui.userDbTable;
  userUiRefs.btnRefreshUsers = m_ui.btnRefreshUsers;
  userUiRefs.btnAddUser = m_ui.btnAddUser;
  userUiRefs.btnEditUser = m_ui.btnEditUser;
  userUiRefs.btnDeleteUser = m_ui.btnDeleteUser;

  UserDbPanelController::Context userContext;
  userContext.service = m_context.userAdminService;
  userContext.logMessage = m_context.logMessage;

  m_userDbPanelController =
      new UserDbPanelController(userUiRefs, userContext, this);

  ZonePanelController::UiRefs zoneUiRefs;
  zoneUiRefs.zoneTable = m_ui.zoneTable;
  zoneUiRefs.btnRefreshZone = m_ui.btnRefreshZone;

  ZonePanelController::Context zoneContext;
  zoneContext.service = m_context.zoneQueryService;
  zoneContext.logMessage = m_context.logMessage;

  m_zonePanelController = new ZonePanelController(zoneUiRefs, zoneContext, this);
}

void DbPanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->connectSignals();
  }
  if (m_userDbPanelController) {
    m_userDbPanelController->connectSignals();
  }
  if (m_zonePanelController) {
    m_zonePanelController->connectSignals();
  }
}

void DbPanelController::refreshAll() {
  onRefreshParkingLogs();
  refreshUserTable();
  refreshZoneTable();
}

void DbPanelController::onRefreshParkingLogs() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onRefreshParkingLogs();
  }
}

void DbPanelController::onSearchParkingLogs() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onSearchParkingLogs();
  }
}

void DbPanelController::onForcePlate() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onForcePlate();
  }
}

void DbPanelController::onEditPlate() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onEditPlate();
  }
}

void DbPanelController::deleteParkingLog() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->deleteParkingLog();
  }
}

void DbPanelController::refreshUserTable() {
  if (m_userDbPanelController) {
    m_userDbPanelController->refreshUserTable();
  }
}

void DbPanelController::deleteUser() {
  if (m_userDbPanelController) {
    m_userDbPanelController->deleteUser();
  }
}

void DbPanelController::addUser() {
  if (m_userDbPanelController) {
    m_userDbPanelController->addUser();
  }
}

void DbPanelController::editUser() {
  if (m_userDbPanelController) {
    m_userDbPanelController->editUser();
  }
}

void DbPanelController::refreshZoneTable() {
  if (m_zonePanelController) {
    m_zonePanelController->refreshZoneTable();
  }
}
