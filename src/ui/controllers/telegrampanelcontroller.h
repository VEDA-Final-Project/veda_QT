#ifndef TELEGRAMPANELCONTROLLER_H
#define TELEGRAMPANELCONTROLLER_H

#include <QObject>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;
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
    QCheckBox *chkShowPlateLogs = nullptr;
    QTextEdit *logView = nullptr;
  };

  explicit TelegramPanelController(const UiRefs &ui, TelegramBotAPI *api,
                                   QObject *parent = nullptr);
  void connectSignals();

public slots:
  void onSendEntry();
  void onSendExit();
  void onTelegramLog(const QString &msg);
  void onUsersUpdated(int count);
  void onPaymentConfirmed(const QString &plate, int amount);
  void onAdminSummoned(const QString &chatId, const QString &name);

signals:
  void usersRefreshed();

private:
  UiRefs m_ui;
  TelegramBotAPI *m_telegramApi = nullptr;
};

#endif // TELEGRAMPANELCONTROLLER_H
