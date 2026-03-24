#ifndef PARKINGLOGPANELCONTROLLER_H
#define PARKINGLOGPANELCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class ParkingLogApplicationService;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class ParkingLogPanelController : public QObject {
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
  };

  struct Context {
    ParkingLogApplicationService *service = nullptr;
    std::function<void(const QString &)> logMessage;
    std::function<void()> refreshVehicleTable;
  };

  explicit ParkingLogPanelController(const UiRefs &uiRefs, Context context,
                                     QObject *parent = nullptr);

  void connectSignals();

public slots:
  void onRefreshParkingLogs();
  void onSearchParkingLogs();
  void onForcePlate();
  void onEditPlate();
  void deleteParkingLog();

private:
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  bool m_signalsConnected = false;
};

#endif // PARKINGLOGPANELCONTROLLER_H
