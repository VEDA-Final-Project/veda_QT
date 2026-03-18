#include "rpipanelcontroller.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>

RpiPanelController::RpiPanelController(const UiRefs &uiRefs, QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
    m_client = new RpiControlClient(this);
    // 기본 접속 대상: IP는 rpicontrolclient.h 기본값(192.168.0.44:12345) 사용
}

void RpiPanelController::connectSignals() {
    if (m_signalsConnected) return;
    m_signalsConnected = true;

    // ── UI 버튼 → 연결/해제 ──────────────────────────────────────────────────
    if (m_ui.btnConnect) {
        connect(m_ui.btnConnect, &QPushButton::clicked,
                this, &RpiPanelController::onConnect);
    }
    if (m_ui.btnDisconnect) {
        connect(m_ui.btnDisconnect, &QPushButton::clicked,
                this, &RpiPanelController::onDisconnect);
    }

    // ── 클라이언트 이벤트 ────────────────────────────────────────────────────
    connect(m_client, &RpiControlClient::connectedChanged,
            this, &RpiPanelController::onConnectedChanged);
    connect(m_client, &RpiControlClient::logMessage,
            this, &RpiPanelController::onLogMessage);

    // ── 제어신호 re-emit ─────────────────────────────────────────────────────
    connect(m_client, &RpiControlClient::joystickMoved,
            this, &RpiPanelController::joystickMoved);
    connect(m_client, &RpiControlClient::encoderRotated,
            this, &RpiPanelController::encoderRotated);
    connect(m_client, &RpiControlClient::encoderClicked,
            this, &RpiPanelController::encoderClicked);
    connect(m_client, &RpiControlClient::channelSelectRequested,
            this, &RpiPanelController::channelSelectRequested);
    connect(m_client, &RpiControlClient::recordingChanged,
            this, &RpiPanelController::recordingChanged);
    connect(m_client, &RpiControlClient::captureRequested,
            this, &RpiPanelController::captureRequested);
    connect(m_client, &RpiControlClient::buttonPressed,
            this, &RpiPanelController::buttonPressed);
    connect(m_client, &RpiControlClient::dbTabChanged,
            this, &RpiPanelController::dbTabChanged);

    // ── 초기 상태 표시 ───────────────────────────────────────────────────────
    onConnectedChanged(false);

    // ── 앱 시작 시 자동 연결 ─────────────────────────────────────────────────
    // UI의 host/port가 설정돼 있으면 그 값 사용, 없으면 기본값 사용
    {
        const QString host = m_ui.hostEdit
                                 ? m_ui.hostEdit->text().trimmed()
                                 : QString();
        const int port = m_ui.portSpin ? m_ui.portSpin->value() : 12345;
        if (!host.isEmpty()) {
            m_client->setServer(host, static_cast<quint16>(port));
        }
        m_client->connectToServer();
    }
}

void RpiPanelController::shutdown() {
    if (m_client) m_client->disconnectFromServer();
}

// ── 버튼 슬롯 ────────────────────────────────────────────────────────────────

void RpiPanelController::onConnect() {
    if (!m_client) return;
    const QString host = m_ui.hostEdit
                             ? m_ui.hostEdit->text().trimmed()
                             : QString();
    const int port = m_ui.portSpin ? m_ui.portSpin->value() : 12345;
    if (!host.isEmpty()) {
        m_client->setServer(host, static_cast<quint16>(port));
    }
    m_client->connectToServer();
}

void RpiPanelController::onDisconnect() {
    if (m_client) m_client->disconnectFromServer();
}

// ── 이벤트 슬롯 ──────────────────────────────────────────────────────────────

void RpiPanelController::onConnectedChanged(bool connected) {
    if (m_ui.connectionStatusLabel) {
        m_ui.connectionStatusLabel->setText(
            connected ? "CONNECTED" : "DISCONNECTED");
        m_ui.connectionStatusLabel->setStyleSheet(
            connected ? "color: #00ff88; font-weight: bold;"
                      : "color: #ff4d4d; font-weight: bold;");
    }
}

void RpiPanelController::onLogMessage(const QString &message) {
    appendLog(message);
    // 수신 명령을 마지막 명령 레이블에도 표시
    if (m_ui.lastCmdLabel && message.contains("RX:")) {
        const int rxIdx = message.indexOf("RX: ");
        if (rxIdx >= 0) {
            m_ui.lastCmdLabel->setText(message.mid(rxIdx + 4).trimmed());
        }
    }
}

void RpiPanelController::appendLog(const QString &message) {
    if (m_ui.logView) {
        m_ui.logView->append(message);
    }
}
