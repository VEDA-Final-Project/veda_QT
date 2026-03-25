#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QPoint>
#include <QString>
#include <QWidget>

class QEvent;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QObject;
class QResizeEvent;
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
  bool eventFilter(QObject *watched, QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

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
  void updateLeftImage();

  RpiAuthClient *authClient_ = nullptr;
  QLabel *leftImageLabel_ = nullptr;
  QLineEdit *idInput_ = nullptr;
  QLineEdit *passwordInput_ = nullptr;
  QLabel *otpHintLabel_ = nullptr;
  QLineEdit *otpInput_ = nullptr;
  QPushButton *loginButton_ = nullptr;
  QPushButton *backButton_ = nullptr;
  QPushButton *closeButton_ = nullptr;
  QLabel *loginStatusLabel_ = nullptr;
  QPoint dragOffset_;
  bool dragInProgress_ = false;
  LoginStep currentStep_ = LoginStep::CredentialsStep;
  QString pendingUsername_;
  QString pendingPassword_;
  bool loginSucceeded_ = false;
};

#endif // LOGINPAGE_H
