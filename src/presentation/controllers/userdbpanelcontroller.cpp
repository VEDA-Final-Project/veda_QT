#include "userdbpanelcontroller.h"

#include "application/db/user/useradminapplicationservice.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <utility>

UserDbPanelController::UserDbPanelController(const UiRefs &uiRefs,
                                             Context context,
                                             QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void UserDbPanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnRefreshUsers) {
    connect(m_ui.btnRefreshUsers, &QPushButton::clicked, this,
            &UserDbPanelController::refreshUserTable);
  }
  if (m_ui.btnAddUser) {
    connect(m_ui.btnAddUser, &QPushButton::clicked, this,
            &UserDbPanelController::addUser);
  }
  if (m_ui.btnEditUser) {
    connect(m_ui.btnEditUser, &QPushButton::clicked, this,
            &UserDbPanelController::editUser);
  }
  if (m_ui.btnDeleteUser) {
    connect(m_ui.btnDeleteUser, &QPushButton::clicked, this,
            &UserDbPanelController::deleteUser);
  }
}

void UserDbPanelController::refreshUserTable() {
  if (!m_ui.userDbTable || !m_context.service) {
    return;
  }

  const QVector<UserRow> users = m_context.service->getAllUsers();

  m_ui.userDbTable->setRowCount(0);
  for (int i = 0; i < users.size(); ++i) {
    const UserRow &user = users[i];
    m_ui.userDbTable->insertRow(i);
    m_ui.userDbTable->setItem(i, 0, new QTableWidgetItem(user.chatId));
    m_ui.userDbTable->setItem(i, 1, new QTableWidgetItem(user.plateNumber));
    m_ui.userDbTable->setItem(i, 2, new QTableWidgetItem(user.name));
    m_ui.userDbTable->setItem(i, 3, new QTableWidgetItem(user.phone));
    m_ui.userDbTable->setItem(i, 4, new QTableWidgetItem(user.paymentInfo));
    m_ui.userDbTable->setItem(i, 5, new QTableWidgetItem(user.createdAt));
  }
}

void UserDbPanelController::addUser() {
  if (!m_ui.userDbTable) {
    return;
  }

  QDialog dlg;
  dlg.setWindowTitle(QString::fromUtf8("사용자 추가"));
  dlg.setFixedSize(360, 280);

  QFormLayout *form = new QFormLayout(&dlg);
  QLineEdit *eChatId = new QLineEdit;
  QLineEdit *ePlate = new QLineEdit;
  QLineEdit *eName = new QLineEdit;
  QLineEdit *ePhone = new QLineEdit;
  QLineEdit *eCard = new QLineEdit;

  form->addRow(QString::fromUtf8("Chat ID:"), eChatId);
  form->addRow(QString::fromUtf8("번호판:"), ePlate);
  form->addRow(QString::fromUtf8("이름:"), eName);
  form->addRow(QString::fromUtf8("연락처:"), ePhone);
  form->addRow(QString::fromUtf8("카드번호:"), eCard);

  QDialogButtonBox *btns =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form->addRow(btns);
  connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  const QString chatId = eChatId->text().trimmed();
  const QString plate = ePlate->text().trimmed();
  if (chatId.isEmpty() || plate.isEmpty()) {
    appendLog("[DB] 사용자 추가 실패: Chat ID와 번호판은 필수입니다.");
    return;
  }

  if (!m_context.service) {
    return;
  }

  const OperationResult result = m_context.service->addUser(
      chatId, UserUpsertInput{plate, eName->text().trimmed(),
                              ePhone->text().trimmed(),
                              eCard->text().trimmed()});
  appendLog(result.message);
  if (result.success && result.shouldRefresh) {
    refreshUserTable();
  }
}

void UserDbPanelController::editUser() {
  if (!m_ui.userDbTable) {
    return;
  }

  const int row = m_ui.userDbTable->currentRow();
  if (row < 0) {
    appendLog("[DB] 수정할 사용자를 먼저 선택해 주세요.");
    return;
  }

  auto cell = [&](int col) {
    QTableWidgetItem *it = m_ui.userDbTable->item(row, col);
    return it ? it->text() : QString();
  };

  const QString chatId = cell(0);

  QDialog dlg;
  dlg.setWindowTitle(QString::fromUtf8("사용자 수정"));
  dlg.setFixedSize(360, 280);

  QFormLayout *form = new QFormLayout(&dlg);
  QLineEdit *ePlate = new QLineEdit(cell(1));
  QLineEdit *eName = new QLineEdit(cell(2));
  QLineEdit *ePhone = new QLineEdit(cell(3));
  QLineEdit *eCard = new QLineEdit(cell(4));

  form->addRow(QString::fromUtf8("번호판:"), ePlate);
  form->addRow(QString::fromUtf8("이름:"), eName);
  form->addRow(QString::fromUtf8("연락처:"), ePhone);
  form->addRow(QString::fromUtf8("카드번호:"), eCard);

  QDialogButtonBox *btns =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form->addRow(btns);
  connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) {
    return;
  }

  if (!m_context.service) {
    return;
  }

  const OperationResult result = m_context.service->updateUser(
      chatId, UserUpsertInput{ePlate->text().trimmed(), eName->text().trimmed(),
                              ePhone->text().trimmed(),
                              eCard->text().trimmed()});
  appendLog(result.message);
  if (result.success && result.shouldRefresh) {
    refreshUserTable();
  }
}

void UserDbPanelController::deleteUser() {
  if (!m_ui.userDbTable) {
    return;
  }

  const int row = m_ui.userDbTable->currentRow();
  if (row < 0) {
    return;
  }

  QTableWidgetItem *chatIdItem = m_ui.userDbTable->item(row, 0);
  if (!chatIdItem) {
    return;
  }

  const QString chatId = chatIdItem->text();
  if (!m_context.service) {
    return;
  }

  const UserDeleteResult result = m_context.service->deleteUser(chatId);
  appendLog(result.message);
  if (result.success && result.shouldRefresh) {
    refreshUserTable();
  }
}

void UserDbPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
