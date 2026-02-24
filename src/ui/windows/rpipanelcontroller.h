#ifndef RPIPANELCONTROLLER_H
#define RPIPANELCONTROLLER_H

#include "rpi/rpitcpclient.h"
#include <QObject>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTextEdit;

class RpiPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QLineEdit *hostEdit = nullptr;
    QSpinBox *portSpin = nullptr;
    QPushButton *btnConnect = nullptr;
    QPushButton *btnDisconnect = nullptr;
    QPushButton *btnBarrierUp = nullptr;
    QPushButton *btnBarrierDown = nullptr;
    QPushButton *btnLedOn = nullptr;
    QPushButton *btnLedOff = nullptr;
    QLabel *connectionStatusLabel = nullptr;
    QLabel *vehicleStatusLabel = nullptr;
    QLabel *ledStatusLabel = nullptr;
    QLabel *irRawLabel = nullptr;
    QLabel *servoAngleLabel = nullptr;
    QTextEdit *logView = nullptr;
  };

  explicit RpiPanelController(const UiRefs &uiRefs, QObject *parent = nullptr);
  void connectSignals();
  void shutdown();

private slots:
  void onRpiConnect();
  void onRpiDisconnect();
  void onRpiBarrierUp();
  void onRpiBarrierDown();
  void onRpiLedOn();
  void onRpiLedOff();
  void onRpiConnectedChanged(bool connected);
  void onRpiParkingStatusUpdated(bool vehicleDetected, bool ledOn, int irRaw,
                                 int servoAngle);
  void onRpiAckReceived(const QString &messageId);
  void onRpiErrReceived(const QString &messageId, const QString &code,
                        const QString &message);
  void onRpiLogMessage(const QString &message);

private:
  void appendLog(const QString &message);

  UiRefs m_ui;
  RpiTcpClient *m_rpiClient = nullptr;
  bool m_signalsConnected = false;
};

#endif // RPIPANELCONTROLLER_H
