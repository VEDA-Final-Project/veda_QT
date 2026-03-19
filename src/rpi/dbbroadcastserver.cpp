#include "dbbroadcastserver.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

DbBroadcastServer::DbBroadcastServer(QObject *parent) : QObject(parent) {
    m_server = new QTcpServer(this);
    m_timer  = new QTimer(this);
    m_timer->setInterval(kBroadcastIntervalMs);
    m_timer->setSingleShot(false);

    connect(m_server, &QTcpServer::newConnection,
            this, &DbBroadcastServer::onNewConnection);
    connect(m_timer, &QTimer::timeout,
            this, &DbBroadcastServer::onBroadcastTimer);
}

DbBroadcastServer::~DbBroadcastServer() {
    stopListening();
}

void DbBroadcastServer::setLogsProvider(std::function<QVector<QJsonObject>()> provider) {
    m_logsProvider = std::move(provider);
}

bool DbBroadcastServer::startListening(quint16 port) {
    if (m_server->isListening()) {
        return true;
    }
    if (!m_server->listen(QHostAddress::Any, port)) {
        emit logMessage(QString("[DB] 브로드캐스트 서버 시작 실패 (포트 %1): %2")
                            .arg(port)
                            .arg(m_server->errorString()));
        return false;
    }
    m_timer->start();
    emit logMessage(QString("[DB] 브로드캐스트 서버 시작 — 포트 %1 (3초 주기)").arg(port));
    return true;
}

void DbBroadcastServer::stopListening() {
    m_timer->stop();
    for (QTcpSocket *client : m_clients) {
        if (client) {
            client->disconnectFromHost();
        }
    }
    m_clients.clear();
    m_server->close();
}

bool DbBroadcastServer::isListening() const {
    return m_server->isListening();
}

int DbBroadcastServer::clientCount() const {
    return m_clients.size();
}

void DbBroadcastServer::broadcastNow() {
    if (m_clients.isEmpty()) {
        return;
    }
    broadcastToAll(buildDbPayload());
}

void DbBroadcastServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        if (!client) {
            continue;
        }
        m_clients.append(client);
        connect(client, &QTcpSocket::disconnected,
                this, &DbBroadcastServer::onClientDisconnected);
        emit logMessage(QString("[DB] RPi 뷰어 연결됨: %1:%2 (연결 수: %3)")
                            .arg(client->peerAddress().toString())
                            .arg(client->peerPort())
                            .arg(m_clients.size()));
        emit clientCountChanged(m_clients.size());

        // 연결 즉시 현재 데이터 전송
        broadcastToAll(buildDbPayload());
    }
}

void DbBroadcastServer::onClientDisconnected() {
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client) {
        return;
    }
    m_clients.removeAll(client);
    client->deleteLater();
    emit logMessage(QString("[DB] RPi 뷰어 연결 종료 (남은 연결 수: %1)").arg(m_clients.size()));
    emit clientCountChanged(m_clients.size());
}

void DbBroadcastServer::onBroadcastTimer() {
    if (m_clients.isEmpty()) {
        return;
    }
    broadcastToAll(buildDbPayload());
}

void DbBroadcastServer::broadcastToAll(const QByteArray &data) {
    for (QTcpSocket *client : m_clients) {
        if (client && client->state() == QAbstractSocket::ConnectedState) {
            client->write(data);
        }
    }
}

QByteArray DbBroadcastServer::buildDbPayload() const {
    QJsonArray rows;

    if (m_logsProvider) {
        const QVector<QJsonObject> logs = m_logsProvider();
        for (const QJsonObject &log : logs) {
            // UI에 표시되는 컬럼과 동일하게 구성
            QJsonObject row;
            row["id"]         = log["id"].toInt();
            row["plate"]      = log["plate_number"].toString();
            row["zone"]       = log["zone_name"].toString().isEmpty()
                                    ? QString("ROI #%1").arg(log["roi_index"].toInt() + 1)
                                    : log["zone_name"].toString();
            row["entry_time"] = log["entry_time"].toString();
            row["exit_time"]  = log["exit_time"].toString();
            row["pay_status"] = log["pay_status"].toString();
            row["fee"]        = log["total_amount"].toInt();
            rows.append(row);
        }
    }

    QJsonObject payload;
    payload["type"] = QStringLiteral("db_data");
    payload["ts"]   = QDateTime::currentMSecsSinceEpoch();
    payload["rows"] = rows;

    return QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n';
}
