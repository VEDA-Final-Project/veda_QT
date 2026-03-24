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
    if (m_host.trimmed().isEmpty() || m_port == 0) {
        emit logMessage("[RPi] 제어 서버 설정이 비어 있어 연결할 수 없습니다.");
        return;
    }
    emit logMessage(
        QString("[RPi] %1:%2 연결 중...").arg(m_host).arg(m_port));
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

void RpiControlClient::sendDbData(const QString &jsonData) {
    if (!isConnected()) return;
    
    // arg() 대신 데이터 결합(concatenation)을 사용하여 특수 문자(%1 등) 처리 안전성 확보
    QByteArray payload = QByteArray("$DB_SYNC,") + jsonData.toUtf8() + QByteArray("\n");
    
    m_socket->write(payload);
    m_socket->flush();
    
    emit logMessage(QString("[RPi] TX DB sync sent (%1 bytes)")
                        .arg(payload.size()));
}

void RpiControlClient::onConnected() {
    resetReconnect();
    emit connectedChanged(true);
    emit logMessage(QString("[RPi] 연결됨 (%1:%2)").arg(m_host).arg(m_port));
}

void RpiControlClient::onDisconnected() {
    emit connectedChanged(false);
    emit logMessage("[RPi] 연결 끊김");
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
        QString("[RPi] 소켓 오류: %1").arg(m_socket->errorString()));
}

void RpiControlClient::onReconnectTimeout() {
    connectToServer();
}

// 패킷 파싱: $<TYPE>[,<d1>[,<d2>]]\n
// 채널 ID 없음 — 모든 필드가 TYPE 바로 다음부터 시작
void RpiControlClient::parsePacket(const QByteArray &rawLine) {
    QByteArray line = rawLine;
    if (line.startsWith('$')) {
        line = line.mid(1);
    }

    const QList<QByteArray> parts = line.split(',');
    if (parts.isEmpty()) return;

    const QString type = QString::fromUtf8(parts[0]).toUpper().trimmed();
    const QString d1   = parts.size() > 1
                             ? QString::fromUtf8(parts[1]).trimmed()
                             : QString();
    const QString d2   = parts.size() > 2
                             ? QString::fromUtf8(parts[2]).trimmed()
                             : QString();

    emit logMessage(QString("[RPi] RX: $%1%2%3")
                        .arg(type)
                        .arg(d1.isEmpty() ? "" : "," + d1)
                        .arg(d2.isEmpty() ? "" : "," + d2));

    if (type == QLatin1String("JOY")) {
        // $JOY,<dir>,<state>
        if ((d1 != QLatin1String("U")) && (d1 != QLatin1String("D")) &&
            (d1 != QLatin1String("L")) && (d1 != QLatin1String("R"))) {
            emit logMessage(QString("[RPi] 잘못된 JOY 방향: %1").arg(d1));
            return;
        }
        if ((d2 != QLatin1String("1")) && (d2 != QLatin1String("0"))) {
            emit logMessage(QString("[RPi] 잘못된 JOY 상태값: %1").arg(d2));
            return;
        }
        const bool active = (d2 == QLatin1String("1"));
        emit joystickMoved(d1, active);

    } else if (type == QLatin1String("ENC")) {
        // $ENC,+1/-1  or  $ENC,CLK
        if (d1 == QLatin1String("CLK")) {
            emit encoderClicked();
        } else {
            bool ok = false;
            const int delta = d1.toInt(&ok);
            if (!ok || (delta != 1 && delta != -1)) {
                emit logMessage(QString("[RPi] 잘못된 ENC 값: %1").arg(d1));
                return;
            }
            emit encoderRotated(delta);
        }

    } else if (type == QLatin1String("CH")) {
        // $CH,SEL
        if (d1 == QLatin1String("SEL")) {
            emit channelSelectRequested();
        }

    } else if (type == QLatin1String("REC")) {
        // $REC,1/0
        emit recordingChanged(d1 == QLatin1String("1"));

    } else if (type == QLatin1String("CAP")) {
        // $CAP,NOW
        emit captureRequested();

    } else if (type == QLatin1String("BTN")) {
        // $BTN,<code>
        bool ok = false;
        const int code = d1.toInt(&ok);
        if (!ok) {
            emit logMessage(QString("[RPi] 잘못된 BTN 값: %1").arg(d1));
            return;
        }
        emit buttonPressed(code);

    } else if (type == QLatin1String("DB")) {
        // $DB,<idx>
        bool ok = false;
        const int idx = d1.toInt(&ok);
        if (!ok) {
            emit logMessage(QString("[RPi] 잘못된 DB 탭 값: %1").arg(d1));
            return;
        }
        emit dbTabChanged(idx);

    } else {
        emit logMessage(QString("[RPi] 알 수 없는 타입: %1").arg(type));
    }
}

void RpiControlClient::scheduleReconnect() {
    if (m_reconnectTimer->isActive()) return;
    const int ms = qMin(kReconnectBaseMs << m_reconnectAttempt, kReconnectMaxMs);
    m_reconnectAttempt = qMin(m_reconnectAttempt + 1, 3);
    emit logMessage(QString("[RPi] %1 ms 후 재연결...").arg(ms));
    m_reconnectTimer->start(ms);
}

void RpiControlClient::resetReconnect() {
    m_reconnectAttempt = 0;
    m_reconnectTimer->stop();
}
