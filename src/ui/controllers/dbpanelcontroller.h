#ifndef DBPANELCONTROLLER_H
#define DBPANELCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <functional>

class ParkingService;
class QDoubleSpinBox;
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
    QLineEdit *forceTypeInput = nullptr;
    QDoubleSpinBox *forceScoreInput = nullptr;
    QLineEdit *forceBBoxInput = nullptr;
    QPushButton *btnForcePlate = nullptr;
    QLineEdit *editPlateInput = nullptr;
    QPushButton *btnEditPlate = nullptr;

    QTableWidget *userDbTable = nullptr;
    QPushButton *btnRefreshUsers = nullptr;
    QPushButton *btnAddUser = nullptr;
    QPushButton *btnEditUser = nullptr;
    QPushButton *btnDeleteUser = nullptr;

    QTableWidget *hwLogTable = nullptr;
    QPushButton *btnRefreshHwLogs = nullptr;
    QPushButton *btnClearHwLogs = nullptr;

    QTableWidget *vehicleTable = nullptr;
    QPushButton *btnRefreshVehicles = nullptr;
    QPushButton *btnDeleteVehicle = nullptr;

    QTableWidget *zoneTable = nullptr;
    QPushButton *btnRefreshZone = nullptr;

    QTextEdit *logView = nullptr;
  };

  struct Context {
    std::function<ParkingService *()> parkingServiceProvider;
    std::function<QVector<QJsonObject>()> primaryZoneRecordsProvider;
    std::function<QVector<QJsonObject>()> secondaryZoneRecordsProvider;
    std::function<void(const QString &)> logMessage;
    std::function<void(const QString &)> userDeleted;
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
  void refreshHwLogs();
  void clearHwLogs();
  void refreshVehicleTable();
  void deleteVehicle();

private:
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  bool m_signalsConnected = false;
};

#endif // DBPANELCONTROLLER_H
