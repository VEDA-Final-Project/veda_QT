#include "rpicontrolclient.h"

#include <QTcpSocket>
#include <QTimer>

RpiControlClient::RpiControlClient(QObject *parent) : QObject(parent) {
    m_socket         = new QTcpSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QTcpSocket::connected,
            this, &RpiControlClient::onConnected);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &RpiControlClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &RpiControlClient::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &RpiControlClient::onSocketError);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &RpiControlClient::onReconnectTimeout);
}

// ── 공개 API ─────────────────────────────────────────────────────────────────

void RpiControlClient::setServer(const QString &host, quint16 port) {
    m_host = host;
    m_port = port;
}

void RpiControlClient::connectToServer() {
    m_shouldReconnect = true;
    resetReconnect();
    if (m_socket->state() == QAbstractSocket::ConnectedState ||
        m_socket->state() == QAbstractSocket::ConnectingState) {
        return;
    }
    emit logMessage(
        QString("[RPi-CTRL] %1:%2 로 연결 중...").arg(m_host).arg(m_port));
    m_socket->connectToHost(m_host, m_port);
}

void RpiControlClient::disconnectFromServer() {
    m_shouldReconnect = false;
    m_reconnectTimer->stop();
    m_socket->disconnectFromHost();
}

bool RpiControlClient::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString RpiControlClient::host() const { return m_host; }
quint16 RpiControlClient::port() const { return m_port; }

// ── 소켓 슬롯 ────────────────────────────────────────────────────────────────

void RpiControlClient::onConnected() {
    resetReconnect();
    emit connectedChanged(true);
    emit logMessage(QString("[RPi-CTRL] RPi 연결됨 (%1:%2)").arg(m_host).arg(m_port));
}

void RpiControlClient::onDisconnected() {
    emit connectedChanged(false);
    emit logMessage("[RPi-CTRL] 연결 끊김");
    if (m_shouldReconnect) {
        scheduleReconnect();
    }
}

void RpiControlClient::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    while (true) {
        const int nl = m_readBuffer.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = m_readBuffer.left(nl).trimmed();
        m_readBuffer.remove(0, nl + 1);
        if (!line.isEmpty()) {
            parsePacket(line);
        }
    }
}

void RpiControlClient::onSocketError() {
    emit logMessage(
        QString("[RPi-CTRL] 소켓 오류: %1").arg(m_socket->errorString()));
}

void RpiControlClient::onReconnectTimeout() {
    connectToServer();
}

// ── 패킷 파싱 ────────────────────────────────────────────────────────────────
// 형식: $<TYPE>,<ch>,<d1>[,<d2>]\n
void RpiControlClient::parsePacket(const QByteArray &rawLine) {
    QByteArray line = rawLine;
    if (line.startsWith('$')) {
        line = line.mid(1);
    }

    const QList<QByteArray> parts = line.split(',');
    if (parts.size() < 3) {
        emit logMessage(
            QString("[RPi-CTRL] 잘못된 패킷: %1").arg(QString::fromUtf8(rawLine)));
        return;
    }

    const QString type = QString::fromUtf8(parts[0]).toUpper().trimmed();
    bool chOk = false;
    const int ch = parts[1].trimmed().toInt(&chOk);
    if (!chOk || ch < 1 || ch > 4) {
        emit logMessage(
            QString("[RPi-CTRL] 잘못된 채널: %1").arg(QString::fromUtf8(rawLine)));
        return;
    }

    const QString d1 = parts.size() > 2
                           ? QString::fromUtf8(parts[2]).trimmed()
                           : QString();
    const QString d2 = parts.size() > 3
                           ? QString::fromUtf8(parts[3]).trimmed()
                           : QString();

    emit logMessage(QString("[RPi-CTRL] RX: $%1,%2,%3%4")
                        .arg(type).arg(ch).arg(d1)
                        .arg(d2.isEmpty() ? "" : "," + d2));

    // ── 명령 분기 ──────────────────────────────────────────────────────────

    if (type == QLatin1String("CH")) {
        // $CH,<ch>,SEL
        if (d1 == QLatin1String("SEL")) {
            emit channelSelectRequested(ch);
        }

    } else if (type == QLatin1String("JOY")) {
        // $JOY,<ch>,<dir>,<state>    dir=U/D/L/R  state=1/0
        const bool active = (d2 == QLatin1String("1"));
        emit joystickMoved(ch, d1, active);

    } else if (type == QLatin1String("ENC")) {
        // $ENC,<ch>,+1/-1  or  $ENC,<ch>,CLK
        if (d1 == QLatin1String("CLK")) {
            emit encoderClicked(ch);
        } else {
            bool ok = false;
            const int delta = d1.toInt(&ok);
            if (ok) {
                emit encoderRotated(ch, delta);
            } else {
                emit logMessage(
                    QString("[RPi-CTRL] ENC 값 오류: %1").arg(d1));
            }
        }

    } else if (type == QLatin1String("REC")) {
        // $REC,<ch>,1/0
        emit recordingChanged(ch, d1 == QLatin1String("1"));

    } else if (type == QLatin1String("CAP")) {
        // $CAP,<ch>,NOW
        emit captureRequested(ch);

    } else if (type == QLatin1String("DB")) {
        // $DB,<ch>,<idx>
        bool ok = false;
        const int idx = d1.toInt(&ok);
        if (ok) {
            emit dbTabChanged(ch, idx);
        }

    } else if (type == QLatin1String("BTN")) {
        // $BTN,<ch>,<code>
        bool ok = false;
        const int code = d1.toInt(&ok);
        if (ok) {
            emit buttonPressed(ch, code);
        }

    } else {
        emit logMessage(
            QString("[RPi-CTRL] 알 수 없는 타입: %1").arg(type));
    }
}

// ── 재연결 ───────────────────────────────────────────────────────────────────

void RpiControlClient::scheduleReconnect() {
    if (m_reconnectTimer->isActive()) return;
    const int ms = qMin(kReconnectBaseMs << m_reconnectAttempt, kReconnectMaxMs);
    m_reconnectAttempt = qMin(m_reconnectAttempt + 1, 3);
    emit logMessage(QString("[RPi-CTRL] %1 ms 후 재연결 시도...").arg(ms));
    m_reconnectTimer->start(ms);
}

void RpiControlClient::resetReconnect() {
    m_reconnectAttempt = 0;
    m_reconnectTimer->stop();
}
