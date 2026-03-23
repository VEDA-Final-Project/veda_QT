#ifndef DBPANELCONTROLLER_H
#define DBPANELCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <functional>

class ParkingLogApplicationService;
class ParkingLogPanelController;
class UserAdminApplicationService;
class UserDbPanelController;
class VehicleInfoPanelController;
class ZonePanelController;
class ZoneQueryApplicationService;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;

class DbPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QTableWidget *parkingLogTable = nullptr;
    QLineEdit *plateSearchInput = nullptr;
    QPushButton *btnSearchPlate = nullptr;
    QPushButton *btnRefreshLogs = nullptr;
    QLineEdit *forcePlateInput = nullptr;
    QSpinBox *forceObjectIdInput = nullptr;
    QPushButton *btnForcePlate = nullptr;
    QLineEdit *editPlateInput = nullptr;
    QPushButton *btnEditPlate = nullptr;

    QTableWidget *userDbTable = nullptr;
    QPushButton *btnRefreshUsers = nullptr;
    QPushButton *btnAddUser = nullptr;
    QPushButton *btnEditUser = nullptr;
    QPushButton *btnDeleteUser = nullptr;

    QTableWidget *vehicleTable = nullptr;
    QPushButton *btnRefreshVehicles = nullptr;
    QPushButton *btnDeleteVehicle = nullptr;

    QTableWidget *zoneTable = nullptr;
    QPushButton *btnRefreshZone = nullptr;

    QTextEdit *logView = nullptr;
  };

  struct Context {
    ParkingLogApplicationService *parkingLogService = nullptr;
    UserAdminApplicationService *userAdminService = nullptr;
    ZoneQueryApplicationService *zoneQueryService = nullptr;
    std::function<void(const QString &)> logMessage;
  };

  explicit DbPanelController(const UiRefs &uiRefs, Context context,
                             QObject *parent = nullptr);

  void connectSignals();
  void refreshAll();
  void refreshZoneTable();

public slots:
  void onRefreshParkingLogs();
  void onSearchParkingLogs();
  void onForcePlate();
  void onEditPlate();
  void deleteParkingLog();
  void refreshUserTable();
  void addUser();
  void editUser();
  void deleteUser();
  void refreshVehicleTable();
  void deleteVehicle();

private:
  UiRefs m_ui;
  Context m_context;
  ParkingLogPanelController *m_parkingLogPanelController = nullptr;
  UserDbPanelController *m_userDbPanelController = nullptr;
  VehicleInfoPanelController *m_vehicleInfoPanelController = nullptr;
  ZonePanelController *m_zonePanelController = nullptr;
  bool m_signalsConnected = false;
};

#endif // DBPANELCONTROLLER_H
