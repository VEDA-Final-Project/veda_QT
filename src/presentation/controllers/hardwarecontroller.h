#ifndef HARDWARECONTROLLER_H
#define HARDWARECONTROLLER_H

#include <QObject>
#include <functional>

class ControllerDialog;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QString;
class VideoWidget;
class RpiControlClient;
class QTimer;
class QTableWidget;

class HardwareController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QPushButton *btnRecordManual = nullptr;
    QStackedWidget *stackedWidget = nullptr;
    QTabWidget *dbSubTabs = nullptr;
    QTableWidget *parkingLogTable = nullptr;
    QTableWidget *userDbTable = nullptr;
    QTableWidget *reidTable = nullptr;
    QTableWidget *zoneTable = nullptr;
  };

  struct Context {
    std::function<void(const QString &)> logMessage;
    std::function<int()> selectedChannelCount;
    std::function<VideoWidget *()> primarySelectedVideoWidget;
    std::function<void()> resetAllChannelZoom;
    std::function<void(int)> selectSingleChannel;
    std::function<void()> captureManual;
    std::function<void(bool)> setManualRecording;
    std::function<bool()> isManualRecording;
    int dbPageIndex = 0;
  };

  explicit HardwareController(const UiRefs &uiRefs, Context context,
                              QObject *parent = nullptr);

  void connectSignals();
  void connectControllerDialog(ControllerDialog *dialog);
  void shutdown();

public slots:
  void syncDbDataToRpi(int tableIdx);

private slots:
  void processJoystickMovement();
  void setHardwareRecordingState(bool recording);
  void navigateHardwareToDbTab(int tabIndex);
  void onHardwareChannelSelectRequested();
  void onHardwareButtonPressed(int btnCode);
  void onHardwareJoystickMoved(const QString &dir, int state);
  void onHardwareEncoderRotated(int delta);
  void onHardwareEncoderClicked();

private:
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  RpiControlClient *m_rpiControlClient = nullptr;
  QTimer *m_joystickTimer = nullptr;
  double m_joystickTargetX = 0.0;
  double m_joystickTargetY = 0.0;
  bool m_signalsConnected = false;
};

#endif // HARDWARECONTROLLER_H
