#include "telegrampanelcontroller.h"
#include "telegram/telegrambotapi.h"
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>

TelegramPanelController::TelegramPanelController(const UiRefs &ui,
                                                 TelegramBotAPI *api,
                                                 QObject *parent)
    : QObject(parent), m_ui(ui), m_telegramApi(api) {}

void TelegramPanelController::connectSignals() {
  if (m_ui.btnSendEntry) {
    connect(m_ui.btnSendEntry, &QPushButton::clicked, this,
            &TelegramPanelController::onSendEntry);
  }
  if (m_ui.btnSendExit) {
    connect(m_ui.btnSendExit, &QPushButton::clicked, this,
            &TelegramPanelController::onSendExit);
  }

  if (m_telegramApi) {
    connect(m_telegramApi, &TelegramBotAPI::logMessage, this,
            &TelegramPanelController::onTelegramLog);
    connect(m_telegramApi, &TelegramBotAPI::usersUpdated, this,
            &TelegramPanelController::onUsersUpdated);
    connect(m_telegramApi, &TelegramBotAPI::paymentConfirmed, this,
            &TelegramPanelController::onPaymentConfirmed);
    connect(m_telegramApi, &TelegramBotAPI::adminSummoned, this,
            &TelegramPanelController::onAdminSummoned);
  }
}

void TelegramPanelController::onSendEntry() {
  if (!m_ui.entryPlateInput || !m_ui.logView) {
    return;
  }

  const QString plate = m_ui.entryPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    m_ui.logView->append("[Telegram] 차량번호를 입력해주세요.");
    return;
  }
  m_telegramApi->sendEntryNotice(plate);
}

void TelegramPanelController::onSendExit() {
  if (!m_ui.exitPlateInput || !m_ui.feeInput || !m_ui.logView) {
    return;
  }

  const QString plate = m_ui.exitPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    m_ui.logView->append("[Telegram] 차량번호를 입력해주세요.");
    return;
  }
  m_telegramApi->sendExitNotice(plate, m_ui.feeInput->value());
}

void TelegramPanelController::onTelegramLog(const QString &msg) {
  if (m_ui.logView) {
    m_ui.logView->append(msg);
  }
}

void TelegramPanelController::onUsersUpdated(int count) {
  if (m_ui.userCountLabel) {
    m_ui.userCountLabel->setText(QString("%1 명").arg(count));
  }

  if (m_ui.userTable && m_telegramApi) {
    const QMap<QString, QString> users = m_telegramApi->getRegisteredUsers();
    m_ui.userTable->setRowCount(0);
    for (auto it = users.begin(); it != users.end(); ++it) {
      const int row = m_ui.userTable->rowCount();
      m_ui.userTable->insertRow(row);
      m_ui.userTable->setItem(row, 0, new QTableWidgetItem(it.key()));
      m_ui.userTable->setItem(row, 1, new QTableWidgetItem(it.value()));
    }
  }

  emit usersRefreshed();
}

void TelegramPanelController::onPaymentConfirmed(const QString &plate,
                                                 int amount) {
  if (m_ui.logView) {
    const QString msg =
        QString("[Payment] 💰 결제 완료 수신! 차량: %1, 금액: %2원")
            .arg(plate)
            .arg(amount);

    if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
      return;
    }
    m_ui.logView->append(msg);
  }
}

void TelegramPanelController::onAdminSummoned(const QString &chatId,
                                              const QString &name) {
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[알림] 🚨 관리자 호출 수신! (User: %1, ChatID: %2)")
            .arg(name, chatId));
  }

  QMessageBox *box = new QMessageBox(nullptr);
  box->setWindowTitle("관리자 호출");
  box->setText(
      QString("🚨 사용자가 관리자를 호출했습니다!\n\n이름: %1\nChat ID: %2")
          .arg(name, chatId));
  box->setIcon(QMessageBox::Warning);
  box->setStandardButtons(QMessageBox::Ok);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowModality(Qt::NonModal);
  box->show();
}
