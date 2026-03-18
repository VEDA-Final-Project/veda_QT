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

// RpiPanelController: RPi 제어신호 수신 관리
//   - 앱 시작 시 192.168.0.44:12345 자동 연결
//   - 수신 신호를 MainWindowController로 re-emit
class RpiPanelController : public QObject {
    Q_OBJECT

public:
    // UI 위젯 참조 (RPi 탭)
    struct UiRefs {
        QLineEdit   *hostEdit              = nullptr;
        QSpinBox    *portSpin              = nullptr;
        QPushButton *btnConnect            = nullptr;
        QPushButton *btnDisconnect         = nullptr;
        QLabel      *connectionStatusLabel = nullptr;
        QLabel      *lastCmdLabel          = nullptr;
        QTextEdit   *logView               = nullptr;
    };

    explicit RpiPanelController(const UiRefs &uiRefs,
                                QObject *parent = nullptr);
    void connectSignals(); // 시그널 연결 + 자동 연결 시작
    void shutdown();

    RpiControlClient *client() const { return m_client; }

signals:
    // ── 상위 컨트롤러로 re-emit ──────────────────────────────────────────────
    void joystickMoved(const QString &dir, bool active);  // $JOY
    void encoderRotated(int delta);                       // $ENC +1/-1
    void encoderClicked();                                // $ENC CLK
    void channelSelectRequested();                        // $CH
    void recordingChanged(bool recording);                // $REC
    void captureRequested();                              // $CAP
    void buttonPressed(int code);                         // $BTN
    void dbTabChanged(int idx);                           // $DB

private slots:
    void onConnect();
    void onDisconnect();
    void onConnectedChanged(bool connected);
    void onLogMessage(const QString &message);

private:
    void appendLog(const QString &message);

    UiRefs           m_ui;
    RpiControlClient *m_client          = nullptr;
    bool              m_signalsConnected = false;
};

#endif // RPIPANELCONTROLLER_H
