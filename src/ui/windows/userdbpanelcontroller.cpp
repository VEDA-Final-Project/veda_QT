#include "userdbpanelcontroller.h"

#include "database/userrepository.h"
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
  if (!m_ui.userDbTable) {
    return;
  }

  UserRepository repo;
  QString error;
  const QVector<QJsonObject> users = repo.getAllUsersFull(&error);

  m_ui.userDbTable->setRowCount(0);
  for (int i = 0; i < users.size(); ++i) {
    const QJsonObject &user = users[i];
    m_ui.userDbTable->insertRow(i);
    m_ui.userDbTable->setItem(i, 0,
                              new QTableWidgetItem(user["chat_id"].toString()));
    m_ui.userDbTable->setItem(
        i, 1, new QTableWidgetItem(user["plate_number"].toString()));
    m_ui.userDbTable->setItem(i, 2,
                              new QTableWidgetItem(user["name"].toString()));
    m_ui.userDbTable->setItem(i, 3,
                              new QTableWidgetItem(user["phone"].toString()));
    m_ui.userDbTable->setItem(
        i, 4, new QTableWidgetItem(user["payment_info"].toString()));
    m_ui.userDbTable->setItem(
        i, 5, new QTableWidgetItem(user["created_at"].toString()));
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

  UserRepository repo;
  QString error;
  if (repo.addUser(chatId, plate, eName->text().trimmed(),
                   ePhone->text().trimmed(), eCard->text().trimmed(),
                   &error)) {
    appendLog(QString("[DB] 사용자 추가 완료: ChatID=%1").arg(chatId));
    refreshUserTable();
    return;
  }

  appendLog(QString("[DB] 사용자 추가 실패: %1").arg(error));
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

  UserRepository repo;
  QString error;
  if (repo.updateUser(chatId, ePlate->text().trimmed(), eName->text().trimmed(),
                      ePhone->text().trimmed(), eCard->text().trimmed(),
                      &error)) {
    appendLog(QString("[DB] 사용자 수정 완료: ChatID=%1").arg(chatId));
    refreshUserTable();
    return;
  }

  appendLog(QString("[DB] 사용자 수정 실패: %1").arg(error));
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

  UserRepository repo;
  QString error;
  if (repo.deleteUser(chatId, &error)) {
    appendLog(QString("[DB] 사용자 삭제 완료: ChatID=%1").arg(chatId));
    if (m_context.userDeleted) {
      m_context.userDeleted(chatId);
    }
    refreshUserTable();
    return;
  }

  appendLog(QString("[DB] 사용자 삭제 실패: %1").arg(error));
}

void UserDbPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
