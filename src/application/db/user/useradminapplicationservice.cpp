#include "application/db/user/useradminapplicationservice.h"

#include "infrastructure/persistence/userrepository.h"
#include <QJsonObject>

UserAdminApplicationService::UserAdminApplicationService(const Context &context,
                                                         QObject *parent)
    : QObject(parent), m_context(context) {}

QVector<UserRow> UserAdminApplicationService::getAllUsers() const {
  UserRepository repo;
  QString error;
  const QVector<QJsonObject> users = repo.getAllUsersFull(&error);

  QVector<UserRow> rows;
  rows.reserve(users.size());
  for (const QJsonObject &user : users) {
    rows.append(UserRow{
        user["chat_id"].toString(),       user["plate_number"].toString(),
        user["name"].toString(),          user["phone"].toString(),
        user["payment_info"].toString(),  user["created_at"].toString(),
    });
  }
  return rows;
}

OperationResult UserAdminApplicationService::addUser(
    const QString &chatId, const UserUpsertInput &input) const {
  const QString normalizedChatId = chatId.trimmed();
  const QString normalizedPlate = input.plateNumber.trimmed();
  if (normalizedChatId.isEmpty() || normalizedPlate.isEmpty()) {
    return {false,
            QStringLiteral("[DB] 사용자 추가 실패: Chat ID와 번호판은 필수입니다."),
            false};
  }

  UserRepository repo;
  QString error;
  if (repo.addUser(normalizedChatId, normalizedPlate, input.name.trimmed(),
                   input.phone.trimmed(), input.paymentInfo.trimmed(),
                   &error)) {
    return {true,
            QString("[DB] 사용자 추가 완료: ChatID=%1").arg(normalizedChatId),
            true};
  }

  return {false, QString("[DB] 사용자 추가 실패: %1").arg(error), false};
}

OperationResult UserAdminApplicationService::updateUser(
    const QString &chatId, const UserUpsertInput &input) const {
  const QString normalizedChatId = chatId.trimmed();
  if (normalizedChatId.isEmpty()) {
    return {false, QStringLiteral("[DB] 사용자 수정 실패: Chat ID가 비어 있습니다."),
            false};
  }

  UserRepository repo;
  QString error;
  if (repo.updateUser(normalizedChatId, input.plateNumber.trimmed(),
                      input.name.trimmed(), input.phone.trimmed(),
                      input.paymentInfo.trimmed(), &error)) {
    return {true,
            QString("[DB] 사용자 수정 완료: ChatID=%1").arg(normalizedChatId),
            true};
  }

  return {false, QString("[DB] 사용자 수정 실패: %1").arg(error), false};
}

UserDeleteResult
UserAdminApplicationService::deleteUser(const QString &chatId) const {
  const QString normalizedChatId = chatId.trimmed();
  if (normalizedChatId.isEmpty()) {
    UserDeleteResult result;
    result.success = false;
    result.message =
        QStringLiteral("[DB] 사용자 삭제 실패: Chat ID가 비어 있습니다.");
    result.shouldRefresh = false;
    return result;
  }

  UserRepository repo;
  QString error;
  if (repo.deleteUser(normalizedChatId, &error)) {
    if (m_context.userDeleted) {
      m_context.userDeleted(normalizedChatId);
    }
    UserDeleteResult result;
    result.success = true;
    result.message =
        QString("[DB] 사용자 삭제 완료: ChatID=%1").arg(normalizedChatId);
    result.shouldRefresh = true;
    result.deletedChatId = normalizedChatId;
    return result;
  }

  UserDeleteResult result;
  result.success = false;
  result.message = QString("[DB] 사용자 삭제 실패: %1").arg(error);
  result.shouldRefresh = false;
  return result;
}
