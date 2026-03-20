#ifndef VEHICLEINFOPANELCONTROLLER_H
#define VEHICLEINFOPANELCONTROLLER_H

#include <QObject>
#include <QString>
#include <functional>

class QPushButton;
class QTableWidget;

class VehicleInfoPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QTableWidget *vehicleTable = nullptr;
    QPushButton *btnRefreshVehicles = nullptr;
    QPushButton *btnDeleteVehicle = nullptr;
  };

  struct Context {
    std::function<void(const QString &)> logMessage;
  };

  explicit VehicleInfoPanelController(const UiRefs &uiRefs, Context context,
                                      QObject *parent = nullptr);

  void connectSignals();

public slots:
  void refreshVehicleTable();
  void deleteVehicle();

private:
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  bool m_signalsConnected = false;
};

#endif // VEHICLEINFOPANELCONTROLLER_H
