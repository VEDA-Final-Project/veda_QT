#ifndef DBBROADCASTSERVER_H
#define DBBROADCASTSERVER_H

#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <functional>

class QTcpServer;
class QTcpSocket;
class QTimer;

/**
 * @brief RPi 터치스크린 DB 뷰어에 주차 로그를 브로드캐스트하는 TCP 서버.
 *
 * QT 앱이 서버 역할을 하며, RPi 측 Python 뷰어(rpi_db_viewer.py)가 클라이언트로 연결합니다.
 * 연결된 모든 클라이언트에게 주기적으로 JSON 한 줄(\n 구분)로 주차 DB 데이터를 전송합니다.
 *
 * 전송 포맷 (한 줄 JSON + \n):
 *   {"type":"db_data","ts":1234567890,"rows":[{"id":1,"plate":"12가3456",...},...]}
 */
class DbBroadcastServer : public QObject {
    Q_OBJECT

public:
    explicit DbBroadcastServer(QObject *parent = nullptr);
    ~DbBroadcastServer() override;

    /**
     * @brief 주차 로그를 제공하는 람다 설정.
     * MainWindowController에서 람다를 주입합니다.
     */
    void setLogsProvider(std::function<QVector<QJsonObject>()> provider);

    /**
     * @brief 지정 포트에서 listen 시작.
     * @return 성공 여부
     */
    bool startListening(quint16 port = 12346);

    void stopListening();

    bool isListening() const;
    int clientCount() const;

    /** 즉시 한 번 브로드캐스트 (타이머 주기 외 수동 호출용) */
    void broadcastNow();

signals:
    void logMessage(const QString &message);
    void clientCountChanged(int count);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onBroadcastTimer();

private:
    void broadcastToAll(const QByteArray &data);
    QByteArray buildDbPayload() const;

    QTcpServer *m_server  = nullptr;
    QTimer     *m_timer   = nullptr;
    QVector<QTcpSocket *> m_clients;
    std::function<QVector<QJsonObject>()> m_logsProvider;

    static constexpr int kBroadcastIntervalMs = 1000; // 1초
};

#endif // DBBROADCASTSERVER_H
