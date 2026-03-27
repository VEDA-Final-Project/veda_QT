#ifndef RPITCPCLIENT_H
#define RPITCPCLIENT_H

#include <QJsonObject>
#include <QObject>

class QTcpSocket;
class QTimer;

class RpiTcpClient : public QObject {
  Q_OBJECT

public:
  explicit RpiTcpClient(QObject *parent = nullptr);
  bool init(); // DB 초기화용

  void setServer(const QString &host, quint16 port);
  void setBarrierAngles(int upAngle, int downAngle);
  void setMockMode(bool enabled);
  bool isMockMode() const;
  void connectToServer();
  void disconnectFromServer();
  bool isConnected() const;

  bool sendLedSet(int value);
  bool sendLedOn();
  bool sendLedOff();
  bool sendServoAngle(int angle);
  bool sendBarrierUp();
  bool sendBarrierDown();
  bool sendPing();

signals:
  void connectedChanged(bool connected);
  void telemetryReceived(const QJsonObject &payload);
  void parkingStatusUpdated(bool vehicleDetected, bool ledOn, int irRaw,
                            int servoAngle);
  void ackReceived(const QString &messageId);
  void errReceived(const QString &messageId, const QString &code,
                   const QString &message);
  void logMessage(const QString &message);

private slots:
  void onConnected();
  void onDisconnected();
  void onReadyRead();
  void onSocketErrorOccurred();
  void onHeartbeatTimeout();
  void onReconnectTimeout();
  void onMockTelemetryTimeout();

private:
  QString nextMessageId(const QString &prefix);
  bool sendCommand(const QString &target, const QString &action,
                   const QJsonObject &payload);
  bool sendMessage(const QJsonObject &message);
  void scheduleReconnect();
  void resetReconnectBackoff();
  void handleRawLine(const QByteArray &line);
  void emitParkingStatus(const QJsonObject &payload);
  void emitMockAck(const QString &prefix);
  void publishMockTelemetry();

  QTcpSocket *m_socket = nullptr;
  QTimer *m_heartbeatTimer = nullptr;
  QTimer *m_reconnectTimer = nullptr;
  QTimer *m_mockTelemetryTimer = nullptr;

  QString m_host = QStringLiteral("127.0.0.1");
  quint16 m_port = 5000;
  QByteArray m_readBuffer;

  int m_reconnectAttempt = 0;
  qint64 m_sequence = 0;
  bool m_shouldReconnect = true;
  bool m_mockMode = false;
  bool m_mockConnected = false;
  bool m_mockVehicleDetected = false;
  int m_mockIrRaw = 700;
  int m_mockLed = 0;
  int m_mockServoAngle = 0;
  int m_barrierUpAngle = 90;
  int m_barrierDownAngle = 0;

};

#endif // RPITCPCLIENT_H
