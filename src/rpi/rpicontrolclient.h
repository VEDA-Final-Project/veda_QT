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
// RPi 송신 프로토콜 (한 줄 = \n 구분):
//   $JOY,<ch>,<dir>,<state>   dir=U/D/L/R  state=1(시작)/0(정지)
//   $ENC,<ch>,+1              엔코더 시계방향 (+1단계 줌인)
//   $ENC,<ch>,-1              엔코더 반시계방향 (-1단계 줌아웃)
//   $ENC,<ch>,CLK             엔코더 클릭 (줌 리셋)
//   $CH,<ch>,SEL              채널 감시 화면 토글
//   $REC,<ch>,1               녹화 시작
//   $REC,<ch>,0               녹화 정지
//   $CAP,<ch>,NOW             캡처
//   $DB,<ch>,<idx>            DB 탭 인덱스 변경
//   $BTN,<ch>,<code>          버튼 입력 (키코드)
//
// ch: 1~4
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
    // 연결 상태 변화
    void connectedChanged(bool connected);

    // $CH,<ch>,SEL — 채널 토글 요청
    void channelSelectRequested(int ch);

    // $JOY,<ch>,<dir>,<state> — dir: U/D/L/R, active: true=이동/false=정지
    void joystickMoved(int ch, const QString &dir, bool active);

    // $ENC,<ch>,+1/-1 — 엔코더 회전 (delta: +1 또는 -1)
    void encoderRotated(int ch, int delta);

    // $ENC,<ch>,CLK — 엔코더 클릭 (줌 리셋)
    void encoderClicked(int ch);

    // $REC,<ch>,1/0 — 녹화 시작/정지
    void recordingChanged(int ch, bool recording);

    // $CAP,<ch>,NOW — 캡처 요청
    void captureRequested(int ch);

    // $DB,<ch>,<idx> — DB 탭 변경
    void dbTabChanged(int ch, int idx);

    // $BTN,<ch>,<code> — 버튼 입력
    void buttonPressed(int ch, int code);

    // 로그 메시지
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

    QTcpSocket *m_socket = nullptr;
    QTimer     *m_reconnectTimer = nullptr;

    QString  m_host = QStringLiteral("192.168.0.9");
    quint16  m_port = 12345;
    QByteArray m_readBuffer;

    int  m_reconnectAttempt = 0;
    bool m_shouldReconnect  = false;

    static constexpr int kReconnectBaseMs = 2000;
    static constexpr int kReconnectMaxMs  = 16000;
};

#endif // RPICONTROLCLIENT_H
