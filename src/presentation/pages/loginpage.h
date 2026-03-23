#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QString>
#include <QWidget>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class RpiAuthClient;

class LoginPage : public QWidget {
  Q_OBJECT

public:
  explicit LoginPage(QWidget *parent = nullptr);

signals:
  void loginSucceeded();
  void loginClosed();

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void handleLogin();
  void handleLoginStepFinished(bool ok, const QString &code,
                               const QString &message);
  void handleAuthFinished(bool ok, const QString &code,
                          const QString &message);

private:
  enum class LoginStep { CredentialsStep, OtpStep };

  void buildUi();
  void showCredentialsStep();
  void setLoginUiBusy(bool busy);
  void showStatusMessage(const QString &message, bool success);
  void showProgressMessage(const QString &message);
  void updateStepUi();
  QString maskedUserId(const QString &userId) const;
  QString messageForAuthCode(const QString &code,
                             const QString &fallbackMessage) const;

  RpiAuthClient *authClient_ = nullptr;
  QLineEdit *idInput_ = nullptr;
  QLineEdit *passwordInput_ = nullptr;
  QLabel *otpHintLabel_ = nullptr;
  QLineEdit *otpInput_ = nullptr;
  QPushButton *loginButton_ = nullptr;
  QPushButton *backButton_ = nullptr;
  QLabel *loginStatusLabel_ = nullptr;
  LoginStep currentStep_ = LoginStep::CredentialsStep;
  QString pendingUsername_;
  QString pendingPassword_;
  bool loginSucceeded_ = false;
};

#endif // LOGINPAGE_H
