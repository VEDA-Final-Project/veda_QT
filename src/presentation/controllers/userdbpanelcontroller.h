#ifndef USERDBPANELCONTROLLER_H
#define USERDBPANELCONTROLLER_H

#include <QObject>
#include <QString>
#include <functional>

class QPushButton;
class QTableWidget;
class UserAdminApplicationService;

class UserDbPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QTableWidget *userDbTable = nullptr;
    QPushButton *btnRefreshUsers = nullptr;
    QPushButton *btnAddUser = nullptr;
    QPushButton *btnEditUser = nullptr;
    QPushButton *btnDeleteUser = nullptr;
  };

  struct Context {
    UserAdminApplicationService *service = nullptr;
    std::function<void(const QString &)> logMessage;
  };

  explicit UserDbPanelController(const UiRefs &uiRefs, Context context,
                                 QObject *parent = nullptr);

  void connectSignals();

public slots:
  void refreshUserTable();
  void addUser();
  void editUser();
  void deleteUser();

private:
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  bool m_signalsConnected = false;
};

#endif // USERDBPANELCONTROLLER_H
