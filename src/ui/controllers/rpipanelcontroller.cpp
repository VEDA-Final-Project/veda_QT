#include "rpipanelcontroller.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>

RpiPanelController::RpiPanelController(const UiRefs &uiRefs, QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
  m_rpiClient = new RpiTcpClient(this);
  m_rpiClient->setBarrierAngles(90, 0);

  if (!m_rpiClient->init()) {
    appendLog("[RPI] 초기화 실패");
  }
}

void RpiPanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnConnect) {
    connect(m_ui.btnConnect, &QPushButton::clicked, this,
            &RpiPanelController::onRpiConnect);
  }
  if (m_ui.btnDisconnect) {
    connect(m_ui.btnDisconnect, &QPushButton::clicked, this,
            &RpiPanelController::onRpiDisconnect);
  }
  if (m_ui.btnBarrierUp) {
    connect(m_ui.btnBarrierUp, &QPushButton::clicked, this,
            &RpiPanelController::onRpiBarrierUp);
  }
  if (m_ui.btnBarrierDown) {
    connect(m_ui.btnBarrierDown, &QPushButton::clicked, this,
            &RpiPanelController::onRpiBarrierDown);
  }
  if (m_ui.btnLedOn) {
    connect(m_ui.btnLedOn, &QPushButton::clicked, this,
            &RpiPanelController::onRpiLedOn);
  }
  if (m_ui.btnLedOff) {
    connect(m_ui.btnLedOff, &QPushButton::clicked, this,
            &RpiPanelController::onRpiLedOff);
  }

  connect(m_rpiClient, &RpiTcpClient::connectedChanged, this,
          &RpiPanelController::onRpiConnectedChanged);
  connect(m_rpiClient, &RpiTcpClient::parkingStatusUpdated, this,
          &RpiPanelController::onRpiParkingStatusUpdated);
  connect(m_rpiClient, &RpiTcpClient::ackReceived, this,
          &RpiPanelController::onRpiAckReceived);
  connect(m_rpiClient, &RpiTcpClient::errReceived, this,
          &RpiPanelController::onRpiErrReceived);
  connect(m_rpiClient, &RpiTcpClient::logMessage, this,
          &RpiPanelController::onRpiLogMessage);

  onRpiConnectedChanged(false);
  onRpiParkingStatusUpdated(false, false, -1, -1);
}

void RpiPanelController::shutdown() {
  if (m_rpiClient) {
    m_rpiClient->disconnectFromServer();
  }
}

void RpiPanelController::onRpiConnect() {
  if (!m_rpiClient) {
    return;
  }

  const QString host =
      m_ui.hostEdit ? m_ui.hostEdit->text().trimmed() : QString();
  const int port = m_ui.portSpin ? m_ui.portSpin->value() : 5000;
  const bool useMock = host.compare("mock", Qt::CaseInsensitive) == 0;

  m_rpiClient->setMockMode(useMock);
  m_rpiClient->setServer(host.isEmpty() ? QStringLiteral("127.0.0.1") : host,
                         static_cast<quint16>(port));
  m_rpiClient->connectToServer();
}

void RpiPanelController::onRpiDisconnect() {
  if (m_rpiClient) {
    m_rpiClient->disconnectFromServer();
  }
}

void RpiPanelController::onRpiBarrierUp() {
  if (!m_rpiClient || !m_rpiClient->sendBarrierUp()) {
    onRpiLogMessage("[RPI] Barrier up command failed");
  }
}

void RpiPanelController::onRpiBarrierDown() {
  if (!m_rpiClient || !m_rpiClient->sendBarrierDown()) {
    onRpiLogMessage("[RPI] Barrier down command failed");
  }
}

void RpiPanelController::onRpiLedOn() {
  if (!m_rpiClient || !m_rpiClient->sendLedOn()) {
    onRpiLogMessage("[RPI] LED on command failed");
  }
}

void RpiPanelController::onRpiLedOff() {
  if (!m_rpiClient || !m_rpiClient->sendLedOff()) {
    onRpiLogMessage("[RPI] LED off command failed");
  }
}

void RpiPanelController::onRpiConnectedChanged(bool connected) {
  if (m_ui.connectionStatusLabel) {
    m_ui.connectionStatusLabel->setText(connected ? "Connected"
                                                  : "Disconnected");
  }
}

void RpiPanelController::onRpiParkingStatusUpdated(bool vehicleDetected,
                                                   bool ledOn, int irRaw,
                                                   int servoAngle) {
  if (m_ui.vehicleStatusLabel) {
    m_ui.vehicleStatusLabel->setText(vehicleDetected ? "Detected" : "Clear");
  }
  if (m_ui.ledStatusLabel) {
    m_ui.ledStatusLabel->setText(ledOn ? "ON" : "OFF");
  }
  if (m_ui.irRawLabel) {
    m_ui.irRawLabel->setText(irRaw >= 0 ? QString::number(irRaw) : "-");
  }
  if (m_ui.servoAngleLabel) {
    m_ui.servoAngleLabel->setText(servoAngle >= 0
                                      ? QString("%1 deg").arg(servoAngle)
                                      : "-");
  }
}

void RpiPanelController::onRpiAckReceived(const QString &messageId) {
  onRpiLogMessage(QString("[RPI] Ack: %1").arg(messageId));
}

void RpiPanelController::onRpiErrReceived(const QString &messageId,
                                          const QString &code,
                                          const QString &message) {
  onRpiLogMessage(QString("[RPI] Error: id=%1 code=%2 message=%3")
                      .arg(messageId)
                      .arg(code)
                      .arg(message));
}

void RpiPanelController::onRpiLogMessage(const QString &message) {
  appendLog(message);
}

void RpiPanelController::appendLog(const QString &message) {
  if (m_ui.logView) {
    m_ui.logView->append(message);
  }
}
