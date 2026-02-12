#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include "camera/camerasessionservice.h"
#include "logging/logdeduplicator.h"
#include "ocr/plateocrcoordinator.h"
#include "rpi/rpitcpclient.h"
#include "roi/roiservice.h"
#include <QComboBox>
#include <QJsonObject>
#include <QObject>
#include <QPushButton>
#include <QTextEdit>

#include "telegram/telegrambotapi.h"

class QLineEdit;
class QSpinBox;
class QLabel;
class VideoWidget;
class QTableWidget;

class MainWindowController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    VideoWidget *videoWidget = nullptr;
    QLineEdit *roiNameEdit = nullptr;
    QComboBox *roiPurposeCombo = nullptr;
    QComboBox *roiSelectorCombo = nullptr;
    QTextEdit *logView = nullptr;
    QPushButton *btnPlay = nullptr;
    QPushButton *btnApplyRoi = nullptr;
    QPushButton *btnFinishRoi = nullptr;
    QPushButton *btnDeleteRoi = nullptr;

    // Telegram Widgets
    QLabel *userCountLabel = nullptr;
    QLineEdit *entryPlateInput = nullptr;
    QPushButton *btnSendEntry = nullptr;
    QLineEdit *exitPlateInput = nullptr;
    QSpinBox *feeInput = nullptr;
    QPushButton *btnSendExit = nullptr;
    QTableWidget *userTable = nullptr;

    // RPi Widgets
    QLineEdit *rpiHostEdit = nullptr;
    QSpinBox *rpiPortSpin = nullptr;
    QPushButton *btnRpiConnect = nullptr;
    QPushButton *btnRpiDisconnect = nullptr;
    QPushButton *btnBarrierUp = nullptr;
    QPushButton *btnBarrierDown = nullptr;
    QPushButton *btnLedOn = nullptr;
    QPushButton *btnLedOff = nullptr;
    QLabel *rpiConnectionStatusLabel = nullptr;
    QLabel *rpiVehicleStatusLabel = nullptr;
    QLabel *rpiLedStatusLabel = nullptr;
    QLabel *rpiIrRawLabel = nullptr;
    QLabel *rpiServoAngleLabel = nullptr;
  };

  explicit MainWindowController(const UiRefs &uiRefs,
                                QObject *parent = nullptr);
  void shutdown();

private:
  void connectSignals();
  void initRoiDb();
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void refreshRoiSelector();
  void playCctv();
  void onLogMessage(const QString &msg);
  void onOcrResult(int objectId, const QString &result);
  void onStartRoiDraw();
  void onFinishRoiDraw();
  void onDeleteRoi();
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onMetadataReceived(const QList<ObjectInfo> &objects);
  void onFrameCaptured(const QImage &frame);

  // Telegram Slots
  void onSendEntry();
  void onSendExit();
  void onTelegramLog(const QString &msg);
  void onUsersUpdated(int count);
  void onPaymentConfirmed(const QString &plate, int amount);

  // RPi Slots
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

  UiRefs m_ui;
  CameraManager *m_cameraManager = nullptr;
  PlateOcrCoordinator *m_ocrCoordinator = nullptr;
  TelegramBotAPI *m_telegramApi = nullptr;
  RpiTcpClient *m_rpiClient = nullptr;
  CameraSessionService m_cameraSession;
  RoiService m_roiService;
  LogDeduplicator m_logDeduplicator;
};

#endif // MAINWINDOWCONTROLLER_H
