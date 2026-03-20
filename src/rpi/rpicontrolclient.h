#ifndef RPICONTROLCLIENT_H
#define RPICONTROLCLIENT_H

#include <QObject>
#include <QString>
#include <QtGlobal>

class QTcpSocket;
class QTimer;

// RpiControlClient: RPi VmsController (TCP 서버) 에 연결해서
// 제어신호 패킷을 수신하는 TCP 클라이언트.
//
// RPi 송신 프로토콜 (한 줄 = \n 구분, 채널 ID 없음):
//   $JOY,<dir>,<state>   dir=U/D/L/R  state=1(시작)/0(정지)
//   $ENC,+1              엔코더 시계방향
//   $ENC,-1              엔코더 반시계방향
//   $ENC,CLK             엔코더 클릭 (줌 리셋)
//   $CH,SEL              채널 감시 화면 토글
//   $REC,1               녹화 시작
//   $REC,0               녹화 정지
//   $CAP,NOW             캡처
//   $BTN,<code>          버튼 입력 (하드웨어 키코드)
//   $DB,<idx>            DB 탭 인덱스 변경
class RpiControlClient : public QObject {
    Q_OBJECT

public:
    explicit RpiControlClient(QObject *parent = nullptr);

    void setServer(const QString &host, quint16 port);
    void connectToServer();
    void disconnectFromServer();
    bool isConnected() const;

    QString host() const;
    quint16 port() const;

signals:
    void connectedChanged(bool connected);

    // $JOY,<dir>,<state> — dir: U/D/L/R, active: true=이동/false=정지
    void joystickMoved(const QString &dir, bool active);

    // $ENC,+1/-1 — 엔코더 회전
    void encoderRotated(int delta);

    // $ENC,CLK — 엔코더 클릭 (줌 리셋)
    void encoderClicked();

    // $CH,SEL — 채널 토글 요청
    void channelSelectRequested();

    // $REC,1/0 — 녹화 시작/정지
    void recordingChanged(bool recording);

    // $CAP,NOW — 캡처 요청
    void captureRequested();

    // $BTN,<code> — 버튼 입력 (하드웨어 키코드)
    void buttonPressed(int code);

    // $DB,<idx> — DB 탭 변경
    void dbTabChanged(int idx);

    void logMessage(const QString &message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError();
    void onReconnectTimeout();

private:
    void parsePacket(const QByteArray &line);
    void scheduleReconnect();
    void resetReconnect();

    QTcpSocket *m_socket         = nullptr;
    QTimer     *m_reconnectTimer = nullptr;

    QString    m_host;
    quint16    m_port = 0;
    QByteArray m_readBuffer;

    int  m_reconnectAttempt = 0;
    bool m_shouldReconnect  = false;

    static constexpr int kReconnectBaseMs = 2000;
    static constexpr int kReconnectMaxMs  = 16000;
};

#endif // RPICONTROLCLIENT_H
