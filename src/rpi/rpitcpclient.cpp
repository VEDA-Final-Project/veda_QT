#include "rpitcpclient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int kHeartbeatMs = 2000;
constexpr int kReconnectBaseMs = 1000;
constexpr int kReconnectMaxMs = 8000;
constexpr int kMockTelemetryMs = 100;
} // namespace

RpiTcpClient::RpiTcpClient(QObject *parent) : QObject(parent) {
  m_socket = new QTcpSocket(this);
  m_heartbeatTimer = new QTimer(this);
  m_reconnectTimer = new QTimer(this);
  m_mockTelemetryTimer = new QTimer(this);

  m_heartbeatTimer->setInterval(kHeartbeatMs);
  m_heartbeatTimer->setSingleShot(false);
  m_reconnectTimer->setSingleShot(true);
  m_mockTelemetryTimer->setInterval(kMockTelemetryMs);
  m_mockTelemetryTimer->setSingleShot(false);

  connect(m_socket, &QTcpSocket::connected, this, &RpiTcpClient::onConnected);
  connect(m_socket, &QTcpSocket::disconnected, this,
          &RpiTcpClient::onDisconnected);
  connect(m_socket, &QTcpSocket::readyRead, this, &RpiTcpClient::onReadyRead);
  connect(m_socket, &QTcpSocket::errorOccurred, this,
          &RpiTcpClient::onSocketErrorOccurred);
  connect(m_heartbeatTimer, &QTimer::timeout, this,
          &RpiTcpClient::onHeartbeatTimeout);
  connect(m_reconnectTimer, &QTimer::timeout, this,
          &RpiTcpClient::onReconnectTimeout);
  connect(m_mockTelemetryTimer, &QTimer::timeout, this,
          &RpiTcpClient::onMockTelemetryTimeout);
}

void RpiTcpClient::setServer(const QString &host, quint16 port) {
  m_host = host;
  m_port = port;
}

void RpiTcpClient::setBarrierAngles(int upAngle, int downAngle) {
  if (upAngle < 0 || upAngle > 180 || downAngle < 0 || downAngle > 180) {
    emit logMessage("[RPI] Invalid barrier angles, expected 0..180");
    return;
  }
  m_barrierUpAngle = upAngle;
  m_barrierDownAngle = downAngle;
}

void RpiTcpClient::setMockMode(bool enabled) {
  if (m_mockMode == enabled) {
    return;
  }
  if (isConnected()) {
    disconnectFromServer();
  }
  m_mockMode = enabled;
  emit logMessage(QString("[RPI] Mock mode: %1").arg(enabled ? "ON" : "OFF"));
}

bool RpiTcpClient::isMockMode() const { return m_mockMode; }

void RpiTcpClient::connectToServer() {
  if (m_mockMode) {
    if (m_mockConnected) {
      return;
    }
    m_mockConnected = true;
    emit connectedChanged(true);
    emit logMessage("[RPI][MOCK] Connected");
    m_mockTelemetryTimer->start();
    publishMockTelemetry();
    return;
  }

  m_shouldReconnect = true;
  if (m_socket->state() == QAbstractSocket::ConnectedState ||
      m_socket->state() == QAbstractSocket::ConnectingState) {
    return;
  }
  emit logMessage(QString("[RPI] Connecting to %1:%2").arg(m_host).arg(m_port));
  m_socket->connectToHost(m_host, m_port);
}

void RpiTcpClient::disconnectFromServer() {
  if (m_mockMode) {
    if (!m_mockConnected) {
      return;
    }
    m_mockConnected = false;
    m_mockTelemetryTimer->stop();
    emit connectedChanged(false);
    emit logMessage("[RPI][MOCK] Disconnected");
    return;
  }

  m_shouldReconnect = false;
  m_heartbeatTimer->stop();
  m_reconnectTimer->stop();
  m_socket->disconnectFromHost();
}

bool RpiTcpClient::isConnected() const {
  if (m_mockMode) {
    return m_mockConnected;
  }
  return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool RpiTcpClient::sendLedSet(int value) {
  if (value != 0 && value != 1) {
    emit logMessage("[RPI] Invalid LED value, expected 0 or 1");
    return false;
  }

  QJsonObject payload;
  payload.insert("value", value);
  if (m_mockMode) {
    m_mockLed = value;
    emitMockAck("mock-led");
    publishMockTelemetry();
    return true;
  }
  return sendCommand("led", "set", payload);
}

bool RpiTcpClient::sendLedOn() { return sendLedSet(1); }

bool RpiTcpClient::sendLedOff() { return sendLedSet(0); }

bool RpiTcpClient::sendServoAngle(int angle) {
  if (angle < 0 || angle > 180) {
    emit logMessage("[RPI] Invalid servo angle, expected 0..180");
    return false;
  }

  QJsonObject payload;
  payload.insert("value", angle);
  if (m_mockMode) {
    m_mockServoAngle = angle;
    emitMockAck("mock-servo");
    publishMockTelemetry();
    return true;
  }
  return sendCommand("servo", "set_angle", payload);
}

bool RpiTcpClient::sendBarrierUp() { return sendServoAngle(m_barrierUpAngle); }

bool RpiTcpClient::sendBarrierDown() {
  return sendServoAngle(m_barrierDownAngle);
}

bool RpiTcpClient::sendPing() {
  if (m_mockMode) {
    return m_mockConnected;
  }

  QJsonObject message;
  message.insert("v", 1);
  message.insert("type", "ping");
  message.insert("id", nextMessageId("p"));
  message.insert("ts", QDateTime::currentMSecsSinceEpoch());
  message.insert("payload", QJsonObject{});
  return sendMessage(message);
}

void RpiTcpClient::onConnected() {
  resetReconnectBackoff();
  m_heartbeatTimer->start();
  emit connectedChanged(true);
  emit logMessage("[RPI] Connected");
}

void RpiTcpClient::onDisconnected() {
  m_heartbeatTimer->stop();
  emit connectedChanged(false);
  emit logMessage("[RPI] Disconnected");
  if (m_shouldReconnect) {
    scheduleReconnect();
  }
}

void RpiTcpClient::onReadyRead() {
  m_readBuffer.append(m_socket->readAll());
  while (true) {
    const int newlineIndex = m_readBuffer.indexOf('\n');
    if (newlineIndex < 0) {
      return;
    }

    const QByteArray line = m_readBuffer.left(newlineIndex).trimmed();
    m_readBuffer.remove(0, newlineIndex + 1);
    if (line.isEmpty()) {
      continue;
    }
    handleRawLine(line);
  }
}

void RpiTcpClient::onSocketErrorOccurred() {
  emit logMessage(QString("[RPI] Socket error: %1").arg(m_socket->errorString()));
}

void RpiTcpClient::onHeartbeatTimeout() {
  if (m_mockMode) {
    return;
  }
  sendPing();
}

void RpiTcpClient::onReconnectTimeout() {
  connectToServer();
}

void RpiTcpClient::onMockTelemetryTimeout() { publishMockTelemetry(); }

QString RpiTcpClient::nextMessageId(const QString &prefix) {
  ++m_sequence;
  return QString("%1-%2").arg(prefix).arg(m_sequence);
}

bool RpiTcpClient::sendCommand(const QString &target, const QString &action,
                               const QJsonObject &payload) {
  QJsonObject commandPayload = payload;
  commandPayload.insert("target", target);
  commandPayload.insert("action", action);

  QJsonObject message;
  message.insert("v", 1);
  message.insert("type", "cmd");
  message.insert("id", nextMessageId("c"));
  message.insert("ts", QDateTime::currentMSecsSinceEpoch());
  message.insert("payload", commandPayload);
  return sendMessage(message);
}

bool RpiTcpClient::sendMessage(const QJsonObject &message) {
  if (!isConnected()) {
    emit logMessage("[RPI] Send failed: not connected");
    return false;
  }

  const QByteArray encoded =
      QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
  const qint64 written = m_socket->write(encoded);
  if (written != encoded.size()) {
    emit logMessage("[RPI] Send failed: partial write");
    return false;
  }
  return true;
}

void RpiTcpClient::scheduleReconnect() {
  if (m_socket->state() == QAbstractSocket::ConnectedState ||
      m_socket->state() == QAbstractSocket::ConnectingState) {
    return;
  }
  if (m_reconnectTimer->isActive()) {
    return;
  }

  const int backoffMs =
      qMin(kReconnectBaseMs << m_reconnectAttempt, kReconnectMaxMs);
  m_reconnectAttempt = qMin(m_reconnectAttempt + 1, 3);

  emit logMessage(QString("[RPI] Reconnect scheduled in %1 ms").arg(backoffMs));
  m_reconnectTimer->start(backoffMs);
}

void RpiTcpClient::resetReconnectBackoff() {
  m_reconnectAttempt = 0;
  m_reconnectTimer->stop();
}

void RpiTcpClient::handleRawLine(const QByteArray &line) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    emit logMessage("[RPI] Dropped invalid JSON line");
    return;
  }

  const QJsonObject message = doc.object();
  const QString type = message.value("type").toString();
  const QString id = message.value("id").toString();
  const QJsonObject payload = message.value("payload").toObject();

  if (type == "telemetry") {
    emit telemetryReceived(payload);
    emitParkingStatus(payload);
    return;
  }
  if (type == "ack") {
    emit ackReceived(id);
    return;
  }
  if (type == "err") {
    emit errReceived(id, payload.value("code").toString(),
                     payload.value("msg").toString());
    return;
  }
  if (type == "pong") {
    return;
  }

  emit logMessage(QString("[RPI] Unhandled message type: %1").arg(type));
}

void RpiTcpClient::emitParkingStatus(const QJsonObject &payload) {
  const int irRaw = payload.value("ir_raw").toInt(-1);
  const bool vehicleDetected = payload.value("ir_detected").toBool(false);
  const bool ledOn = payload.value("led").toInt(0) == 1;
  const int servoAngle = payload.value("servo_angle").toInt(-1);
  emit parkingStatusUpdated(vehicleDetected, ledOn, irRaw, servoAngle);
}

void RpiTcpClient::emitMockAck(const QString &prefix) {
  emit ackReceived(nextMessageId(prefix));
}

void RpiTcpClient::publishMockTelemetry() {
  if (!m_mockMode || !m_mockConnected) {
    return;
  }

  m_mockVehicleDetected = !m_mockVehicleDetected;
  m_mockIrRaw = m_mockVehicleDetected ? 840 : 710;

  QJsonObject payload;
  payload.insert("ir_raw", m_mockIrRaw);
  payload.insert("ir_detected", m_mockVehicleDetected);
  payload.insert("led", m_mockLed);
  payload.insert("servo_angle", m_mockServoAngle);

  emit telemetryReceived(payload);
  emitParkingStatus(payload);
}
