#ifndef TELEGRAMPANELCONTROLLER_H
#define TELEGRAMPANELCONTROLLER_H

#include <QObject>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QString;
class TelegramBotAPI;

class TelegramPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QLabel *userCountLabel = nullptr;
    QLineEdit *entryPlateInput = nullptr;
    QPushButton *btnSendEntry = nullptr;
    QLineEdit *exitPlateInput = nullptr;
    QSpinBox *feeInput = nullptr;
    QPushButton *btnSendExit = nullptr;
    QTableWidget *userTable = nullptr;
  };

  struct Context {
    std::function<void(const QString &)> logMessage;
    std::function<void()> refreshUserTable;
    std::function<void()> refreshParkingLogs;
  };

  explicit TelegramPanelController(const UiRefs &uiRefs, Context context,
                                   QObject *parent = nullptr);

  void connectSignals();
  void shutdown();
  TelegramBotAPI *api() const;

private slots:
  void onSendEntry();
  void onSendExit();
  void onTelegramLog(const QString &msg);
  void onUsersUpdated(int count);
  void onPaymentConfirmed(int recordId, const QString &plate, int amount);
  void onAdminSummoned(const QString &chatId, const QString &name,
                       const QString &phone);


private:
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  TelegramBotAPI *m_api = nullptr;
  bool m_signalsConnected = false;
};

#endif // TELEGRAMPANELCONTROLLER_H
