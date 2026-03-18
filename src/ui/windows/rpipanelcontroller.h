#ifndef RPIPANELCONTROLLER_H
#define RPIPANELCONTROLLER_H

#include "rpi/rpicontrolclient.h"
#include <QObject>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTextEdit;

// RpiPanelController: RPI 제어 탭 로직
//   - RpiControlClient 소유 및 연결 관리
//   - RPi → NVR 방향 제어신호 수신 후 상위 시그널 re-emit
class RpiPanelController : public QObject {
    Q_OBJECT

public:
    struct UiRefs {
        // 연결 설정
        QLineEdit   *hostEdit              = nullptr;
        QSpinBox    *portSpin              = nullptr;
        QPushButton *btnConnect            = nullptr;
        QPushButton *btnDisconnect         = nullptr;
        // 수신 상태 표시
        QLabel      *connectionStatusLabel = nullptr;
        QLabel      *lastCmdLabel          = nullptr;   // 마지막 수신 명령
        // 로그
        QTextEdit   *logView               = nullptr;
    };

    explicit RpiPanelController(const UiRefs &uiRefs,
                                QObject *parent = nullptr);
    void connectSignals();
    void shutdown();

    RpiControlClient *client() const { return m_client; }

signals:
    // 상위(MainWindowController)로 re-emit 하는 제어 신호
    void channelSelectRequested(int ch);                          // $CH
    void joystickMoved(int ch, const QString &dir, bool active); // $JOY
    void encoderRotated(int ch, int delta);                      // $ENC +1/-1
    void encoderClicked(int ch);                                 // $ENC CLK
    void recordingChanged(int ch, bool recording);               // $REC
    void captureRequested(int ch);                               // $CAP

private slots:
    void onConnect();
    void onDisconnect();
    void onConnectedChanged(bool connected);
    void onLogMessage(const QString &message);
    void onChannelSelect(int ch);
    void onJoystick(int ch, const QString &dir, bool active);
    void onEncoderRotated(int ch, int delta);
    void onEncoderClicked(int ch);
    void onRecordingChanged(int ch, bool recording);
    void onCaptureRequested(int ch);

private:
    void appendLog(const QString &message);

    UiRefs           m_ui;
    RpiControlClient *m_client          = nullptr;
    bool              m_signalsConnected = false;
};

#endif // RPIPANELCONTROLLER_H
