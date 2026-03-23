#ifndef APPLICATION_DB_USER_USERADMINAPPLICATIONSERVICE_H
#define APPLICATION_DB_USER_USERADMINAPPLICATIONSERVICE_H

#include "application/db/common/operationresult.h"
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

struct UserRow {
  QString chatId;
  QString plateNumber;
  QString name;
  QString phone;
  QString paymentInfo;
  QString createdAt;
};

struct UserUpsertInput {
  QString plateNumber;
  QString name;
  QString phone;
  QString paymentInfo;
};

struct UserDeleteResult : public OperationResult {
  QString deletedChatId;
};

class UserAdminApplicationService : public QObject {
  Q_OBJECT

public:
  struct Context {
    std::function<void(const QString &)> userDeleted;
  };

  explicit UserAdminApplicationService(const Context &context = {},
                                       QObject *parent = nullptr);

  QVector<UserRow> getAllUsers() const;
  OperationResult addUser(const QString &chatId,
                          const UserUpsertInput &input) const;
  OperationResult updateUser(const QString &chatId,
                             const UserUpsertInput &input) const;
  UserDeleteResult deleteUser(const QString &chatId) const;

private:
  Context m_context;
};

#endif // APPLICATION_DB_USER_USERADMINAPPLICATIONSERVICE_H
