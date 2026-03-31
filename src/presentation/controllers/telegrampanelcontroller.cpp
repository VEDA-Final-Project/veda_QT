#include "telegrampanelcontroller.h"

#include "infrastructure/telegram/telegrambotapi.h"
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <utility>

TelegramPanelController::TelegramPanelController(const UiRefs &uiRefs,
                                                 Context context,
                                                 QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)),
      m_api(new TelegramBotAPI(this)) {}

void TelegramPanelController::connectSignals() {
  if (m_signalsConnected || !m_api) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnSendEntry) {
    connect(m_ui.btnSendEntry, &QPushButton::clicked, this,
            &TelegramPanelController::onSendEntry);
  }
  if (m_ui.btnSendExit) {
    connect(m_ui.btnSendExit, &QPushButton::clicked, this,
            &TelegramPanelController::onSendExit);
  }

  connect(m_api, &TelegramBotAPI::logMessage, this,
          &TelegramPanelController::onTelegramLog);
  connect(m_api, &TelegramBotAPI::usersUpdated, this,
          &TelegramPanelController::onUsersUpdated);
  connect(m_api, &TelegramBotAPI::paymentConfirmed, this,
          &TelegramPanelController::onPaymentConfirmed);
  connect(m_api, &TelegramBotAPI::adminSummoned, this,
          &TelegramPanelController::onAdminSummoned);
}

void TelegramPanelController::shutdown() {
  if (m_api) {
    m_api->shutdown();
  }
}

TelegramBotAPI *TelegramPanelController::api() const { return m_api; }

void TelegramPanelController::onSendEntry() {
  if (!m_ui.entryPlateInput || !m_api) {
    return;
  }

  const QString plate = m_ui.entryPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    appendLog("[Telegram] 차량번호를 입력해주세요.");
    return;
  }
  m_api->sendEntryNotice(plate);
}

void TelegramPanelController::onSendExit() {
  if (!m_ui.exitPlateInput || !m_ui.feeInput || !m_api) {
    return;
  }

  const QString plate = m_ui.exitPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    appendLog("[Telegram] 차량번호를 입력해주세요.");
    return;
  }
  m_api->sendExitNotice(plate, m_ui.feeInput->value());
}

void TelegramPanelController::onTelegramLog(const QString &msg) { appendLog(msg); }

void TelegramPanelController::onUsersUpdated(int count) {
  if (m_ui.userCountLabel) {
    m_ui.userCountLabel->setText(QString("%1 명").arg(count));
  }

  if (m_ui.userTable && m_api) {
    const QMap<QString, QString> users = m_api->getRegisteredUsers();
    m_ui.userTable->setRowCount(0);
    for (auto it = users.begin(); it != users.end(); ++it) {
      const int row = m_ui.userTable->rowCount();
      m_ui.userTable->insertRow(row);
      m_ui.userTable->setItem(row, 0, new QTableWidgetItem(it.key()));
      m_ui.userTable->setItem(row, 1, new QTableWidgetItem(it.value()));
    }
  }

  if (m_context.refreshUserTable) {
    m_context.refreshUserTable();
  }
}

void TelegramPanelController::onPaymentConfirmed(int recordId,
                                                 const QString &plate,
                                                 int amount) {
  appendLog(QString("[Payment] 결제 완료 수신: ID=%1, 차량: %2, 금액: %3원")
                .arg(recordId)
                .arg(plate)
                .arg(amount));

  if (m_context.refreshParkingLogs) {
    m_context.refreshParkingLogs();
  }
}

void TelegramPanelController::onAdminSummoned(const QString &chatId,
                                              const QString &name,
                                              const QString &phone) {
  appendLog(QString("[알림] 관리자 호출 수신 (User: %1, Phone: %2, ChatID: %3)")
                .arg(name, phone, chatId));

  QMessageBox *box = new QMessageBox(nullptr);
  box->setWindowTitle("관리자 호출");
  box->setText(
      QString("사용자가 관리자를 호출했습니다.\n\n이름: %1\n전화번호: %2")
          .arg(name, phone));
  box->setIcon(QMessageBox::Warning);
  box->setStandardButtons(QMessageBox::Ok);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowModality(Qt::NonModal);
  box->show();
}

void TelegramPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
