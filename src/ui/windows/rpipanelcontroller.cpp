#include "rpipanelcontroller.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>

RpiPanelController::RpiPanelController(const UiRefs &uiRefs, QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
    m_client = new RpiControlClient(this);
}

void RpiPanelController::connectSignals() {
    if (m_signalsConnected) return;
    m_signalsConnected = true;

    // ── UI 버튼 → 클라이언트 연결/해제 ───────────────────────────────────
    if (m_ui.btnConnect) {
        connect(m_ui.btnConnect, &QPushButton::clicked,
                this, &RpiPanelController::onConnect);
    }
    if (m_ui.btnDisconnect) {
        connect(m_ui.btnDisconnect, &QPushButton::clicked,
                this, &RpiPanelController::onDisconnect);
    }

    // ── 클라이언트 → 패널 슬롯 ───────────────────────────────────────────
    connect(m_client, &RpiControlClient::connectedChanged,
            this, &RpiPanelController::onConnectedChanged);
    connect(m_client, &RpiControlClient::logMessage,
            this, &RpiPanelController::onLogMessage);
    connect(m_client, &RpiControlClient::channelSelectRequested,
            this, &RpiPanelController::onChannelSelect);
    connect(m_client, &RpiControlClient::joystickMoved,
            this, &RpiPanelController::onJoystick);
    connect(m_client, &RpiControlClient::encoderRotated,
            this, &RpiPanelController::onEncoderRotated);
    connect(m_client, &RpiControlClient::encoderClicked,
            this, &RpiPanelController::onEncoderClicked);
    connect(m_client, &RpiControlClient::recordingChanged,
            this, &RpiPanelController::onRecordingChanged);
    connect(m_client, &RpiControlClient::captureRequested,
            this, &RpiPanelController::onCaptureRequested);

    // 초기 상태 반영
    onConnectedChanged(false);
}

void RpiPanelController::shutdown() {
    if (m_client) {
        m_client->disconnectFromServer();
    }
}

// ── UI 버튼 슬롯 ─────────────────────────────────────────────────────────────

void RpiPanelController::onConnect() {
    if (!m_client) return;
    const QString host = m_ui.hostEdit
                             ? m_ui.hostEdit->text().trimmed()
                             : QStringLiteral("192.168.0.100");
    const int port = m_ui.portSpin ? m_ui.portSpin->value() : 12345;
    m_client->setServer(host.isEmpty() ? QStringLiteral("192.168.0.100") : host,
                        static_cast<quint16>(port));
    m_client->connectToServer();
}

void RpiPanelController::onDisconnect() {
    if (m_client) m_client->disconnectFromServer();
}

// ── 클라이언트 이벤트 ────────────────────────────────────────────────────────

void RpiPanelController::onConnectedChanged(bool connected) {
    if (m_ui.connectionStatusLabel) {
        m_ui.connectionStatusLabel->setText(connected ? "CONNECTED" : "DISCONNECTED");
        m_ui.connectionStatusLabel->setStyleSheet(
            connected ? "color: #00ff88; font-weight: bold;"
                      : "color: #ff4d4d; font-weight: bold;");
    }
    appendLog(connected ? "[RPi-CTRL] 연결됨" : "[RPi-CTRL] 연결 끊김");
}

void RpiPanelController::onLogMessage(const QString &message) {
    appendLog(message);
}

void RpiPanelController::onChannelSelect(int ch) {
    const QString msg =
        QString("[RPi-CTRL] CH 토글 요청: CH%1").arg(ch);
    appendLog(msg);
    if (m_ui.lastCmdLabel)
        m_ui.lastCmdLabel->setText(QString("$CH,%1,SEL").arg(ch));
    emit channelSelectRequested(ch);
}

void RpiPanelController::onJoystick(int ch, const QString &dir, bool active) {
    const QString msg =
        QString("[RPi-CTRL] JOY CH%1 %2 %3").arg(ch).arg(dir).arg(active ? "시작" : "정지");
    appendLog(msg);
    if (m_ui.lastCmdLabel)
        m_ui.lastCmdLabel->setText(
            QString("$JOY,%1,%2,%3").arg(ch).arg(dir).arg(active ? 1 : 0));
    emit joystickMoved(ch, dir, active);
}

void RpiPanelController::onEncoderRotated(int ch, int delta) {
    const QString msg =
        QString("[RPi-CTRL] ENC CH%1 %2").arg(ch).arg(delta > 0 ? "+1" : "-1");
    appendLog(msg);
    if (m_ui.lastCmdLabel)
        m_ui.lastCmdLabel->setText(
            QString("$ENC,%1,%2").arg(ch).arg(delta > 0 ? "+1" : "-1"));
    emit encoderRotated(ch, delta);
}

void RpiPanelController::onEncoderClicked(int ch) {
    const QString msg = QString("[RPi-CTRL] ENC CH%1 CLK (줌 리셋)").arg(ch);
    appendLog(msg);
    if (m_ui.lastCmdLabel)
        m_ui.lastCmdLabel->setText(QString("$ENC,%1,CLK").arg(ch));
    emit encoderClicked(ch);
}

void RpiPanelController::onRecordingChanged(int ch, bool recording) {
    const QString msg =
        QString("[RPi-CTRL] REC CH%1 %2").arg(ch).arg(recording ? "시작" : "정지");
    appendLog(msg);
    if (m_ui.lastCmdLabel)
        m_ui.lastCmdLabel->setText(
            QString("$REC,%1,%2").arg(ch).arg(recording ? 1 : 0));
    emit recordingChanged(ch, recording);
}

void RpiPanelController::onCaptureRequested(int ch) {
    const QString msg = QString("[RPi-CTRL] CAP CH%1 캡처 요청").arg(ch);
    appendLog(msg);
    if (m_ui.lastCmdLabel)
        m_ui.lastCmdLabel->setText(QString("$CAP,%1,NOW").arg(ch));
    emit captureRequested(ch);
}

void RpiPanelController::appendLog(const QString &message) {
    if (m_ui.logView) {
        m_ui.logView->append(message);
    }
}
