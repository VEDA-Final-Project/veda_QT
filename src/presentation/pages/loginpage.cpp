#include "loginpage.h"
#include "loginpageview.h"

#include "config/config.h"
#include "infrastructure/rpi/rpiauthclient.h"

#include <QCloseEvent>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>

LoginPage::LoginPage(QWidget *parent) : QWidget(parent) {
  authClient_ = new RpiAuthClient(this);
  authClient_->setTimeouts(Config::instance().authConnectTimeoutMs(),
                           Config::instance().authRequestTimeoutMs());
  setWindowFlag(Qt::FramelessWindowHint, true);
  buildUi();
  connect(authClient_, &RpiAuthClient::loginStepFinished, this,
          &LoginPage::handleLoginStepFinished);
  connect(authClient_, &RpiAuthClient::authFinished, this,
          &LoginPage::handleAuthFinished);
  setWindowTitle(QStringLiteral("로그인"));
  resize(840, 540);
}

void LoginPage::buildUi() {
  const LoginPageUiRefs ui = buildLoginPageUi(this);
  leftImageLabel_ = ui.leftImageLabel;
  idInput_ = ui.idInput;
  passwordInput_ = ui.passwordInput;
  otpHintLabel_ = ui.otpHintLabel;
  otpInput_ = ui.otpInput;
  loginButton_ = ui.loginButton;
  backButton_ = ui.backButton;
  closeButton_ = ui.closeButton;
  loginStatusLabel_ = ui.loginStatusLabel;

  connect(loginButton_, &QPushButton::clicked, this, &LoginPage::handleLogin);
  connect(backButton_, &QPushButton::clicked, this,
          &LoginPage::showCredentialsStep);
  connect(closeButton_, &QPushButton::clicked, this, &QWidget::close);
  connect(idInput_, &QLineEdit::returnPressed, this, &LoginPage::handleLogin);
  connect(passwordInput_, &QLineEdit::returnPressed, this,
          &LoginPage::handleLogin);
  connect(otpInput_, &QLineEdit::returnPressed, this, &LoginPage::handleLogin);

  installEventFilter(this);
  const auto widgets = findChildren<QWidget *>();
  for (QWidget *widget : widgets) {
    widget->installEventFilter(this);
  }

  updateLeftImage();
  updateStepUi();
}

bool LoginPage::eventFilter(QObject *watched, QEvent *event) {
  if (qobject_cast<QLineEdit *>(watched) || qobject_cast<QPushButton *>(watched)) {
    return QWidget::eventFilter(watched, event);
  }

  switch (event->type()) {
  case QEvent::MouseButtonPress: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      dragInProgress_ = true;
      dragOffset_ = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
      return true;
    }
    break;
  }
  case QEvent::MouseMove: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (dragInProgress_ && (mouseEvent->buttons() & Qt::LeftButton)) {
      move(mouseEvent->globalPosition().toPoint() - dragOffset_);
      return true;
    }
    break;
  }
  case QEvent::MouseButtonRelease: {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      dragInProgress_ = false;
      return true;
    }
    break;
  }
  default:
    break;
  }

  return QWidget::eventFilter(watched, event);
}

void LoginPage::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  updateLeftImage();
}

void LoginPage::handleLogin() {
  if (!idInput_ || !passwordInput_ || !otpInput_ || !loginStatusLabel_ ||
      !authClient_) {
    return;
  }

  if (currentStep_ == LoginStep::CredentialsStep) {
    const QString username = idInput_->text().trimmed();
    const QString password = passwordInput_->text();
    if (username.isEmpty() || password.isEmpty()) {
      showStatusMessage(QStringLiteral("아이디와 비밀번호를 모두 입력하세요."),
                        false);
      return;
    }

    pendingUsername_ = username;
    pendingPassword_ = password;
    setLoginUiBusy(true);
    showProgressMessage(QStringLiteral("아이디와 비밀번호를 확인 중입니다..."));

    const bool started = authClient_->beginLogin(
        Config::instance().authHost(),
        static_cast<quint16>(Config::instance().authPort()), pendingUsername_,
        pendingPassword_);
    if (!started) {
      setLoginUiBusy(false);
      showStatusMessage(QStringLiteral("이미 인증 요청이 진행 중입니다."),
                        false);
    }
    return;
  }

  const QString otp = otpInput_->text().trimmed();
  if (otp.isEmpty()) {
    showStatusMessage(QStringLiteral("OTP를 입력하세요."), false);
    return;
  }
  if (otp.size() != 6) {
    showStatusMessage(QStringLiteral("OTP는 6자리 숫자로 입력하세요."), false);
    return;
  }

  setLoginUiBusy(true);
  showProgressMessage(QStringLiteral("2차 인증 코드를 확인 중입니다..."));

  const bool started = authClient_->submitTotp(otp);
  if (!started) {
    setLoginUiBusy(false);
    showCredentialsStep();
    showStatusMessage(
        QStringLiteral("인증 세션이 종료되었습니다. 다시 로그인해주세요."),
        false);
  }
}

void LoginPage::handleLoginStepFinished(bool ok, const QString &code,
                                        const QString &message) {
  if (ok) {
    currentStep_ = LoginStep::OtpStep;
    updateStepUi();
    setLoginUiBusy(false);
    showProgressMessage(
        message.isEmpty()
            ? QStringLiteral("아이디와 비밀번호 확인 완료. OTP를 입력하세요.")
            : message);
    if (otpInput_) {
      otpInput_->clear();
      otpInput_->setFocus(Qt::OtherFocusReason);
    }
    return;
  }

  setLoginUiBusy(false);
  if (code == QStringLiteral("invalid_credentials") && passwordInput_) {
    passwordInput_->clear();
  }
  showStatusMessage(messageForAuthCode(code, message), false);
}

void LoginPage::handleAuthFinished(bool ok, const QString &code,
                                   const QString &message) {
  setLoginUiBusy(false);

  if (ok) {
    loginSucceeded_ = true;
    showStatusMessage(QStringLiteral("인증 성공"), true);
    emit loginSucceeded();
    return;
  }

  if (code == QStringLiteral("invalid_credentials")) {
    showCredentialsStep();
    showStatusMessage(messageForAuthCode(code, message), false);
    return;
  }
  if (code == QStringLiteral("invalid_otp")) {
    otpInput_->clear();
    otpInput_->setFocus();
    showStatusMessage(messageForAuthCode(code, message), false);
    return;
  }

  showCredentialsStep();
  showStatusMessage(messageForAuthCode(code, message), false);
}

void LoginPage::showCredentialsStep() {
  if (authClient_) {
    authClient_->cancel();
  }

  currentStep_ = LoginStep::CredentialsStep;
  otpInput_->clear();
  loginStatusLabel_->clear();
  loginStatusLabel_->setVisible(false);
  pendingUsername_.clear();
  pendingPassword_.clear();
  setLoginUiBusy(false);
  updateStepUi();

  if (idInput_) {
    idInput_->setVisible(true);
    idInput_->setEnabled(true);
    idInput_->setReadOnly(false);
  }
  if (passwordInput_) {
    passwordInput_->setVisible(true);
    passwordInput_->setEnabled(true);
    passwordInput_->setReadOnly(false);
  }
  if (otpInput_) {
    otpInput_->setEnabled(false);
  }

  QMetaObject::invokeMethod(
      this,
      [this]() {
        if (passwordInput_) {
          passwordInput_->setFocus(Qt::OtherFocusReason);
          passwordInput_->selectAll();
        }
      },
      Qt::QueuedConnection);
}

void LoginPage::setLoginUiBusy(bool busy) {
  const bool credentialsStep = currentStep_ == LoginStep::CredentialsStep;
  if (idInput_) {
    idInput_->setEnabled(credentialsStep && !busy);
  }
  if (passwordInput_) {
    passwordInput_->setEnabled(credentialsStep && !busy);
  }
  if (otpInput_) {
    otpInput_->setEnabled(!credentialsStep && !busy);
  }
  if (backButton_) {
    backButton_->setEnabled(!credentialsStep && !busy);
  }
  if (loginButton_) {
    loginButton_->setEnabled(!busy);
  }
}

void LoginPage::showStatusMessage(const QString &message, bool success) {
  if (!loginStatusLabel_) {
    return;
  }

  loginStatusLabel_->setVisible(true);
  loginStatusLabel_->setText(message);
  loginStatusLabel_->setStyleSheet(
      success
          ? QStringLiteral(
                "color: #10B981; font-size: 12px; font-weight: 700;")
          : QStringLiteral(
                "color: #EF4444; font-size: 12px; font-weight: 700;"));
}

void LoginPage::showProgressMessage(const QString &message) {
  if (!loginStatusLabel_) {
    return;
  }

  loginStatusLabel_->setVisible(true);
  loginStatusLabel_->setText(message);
  loginStatusLabel_->setStyleSheet(
      QStringLiteral("color: #CBD5E1; font-size: 12px; font-weight: 700;"));
}

void LoginPage::updateStepUi() {
  const bool otpStep = currentStep_ == LoginStep::OtpStep;

  if (idInput_) {
    idInput_->setVisible(!otpStep);
  }
  if (passwordInput_) {
    passwordInput_->setVisible(!otpStep);
  }
  if (otpHintLabel_) {
    otpHintLabel_->setVisible(otpStep);
    otpHintLabel_->setText(otpStep
                               ? QStringLiteral("%1 계정의 OTP를 입력하세요.")
                                     .arg(maskedUserId(pendingUsername_))
                               : QString());
  }
  if (otpInput_) {
    otpInput_->setVisible(otpStep);
    otpInput_->setReadOnly(!otpStep);
  }
  if (backButton_) {
    backButton_->setVisible(otpStep);
  }
  if (loginButton_) {
    loginButton_->setText(otpStep ? QStringLiteral("인증하고 시작")
                                  : QStringLiteral("다음"));
  }
}

QString LoginPage::maskedUserId(const QString &userId) const {
  if (userId.size() <= 2) {
    return QString(userId.size(), QChar('*'));
  }
  QString masked = userId;
  for (int i = 1; i < masked.size() - 1; ++i) {
    masked[i] = QChar('*');
  }
  return masked;
}

QString LoginPage::messageForAuthCode(const QString &code,
                                      const QString &fallbackMessage) const {
  if (code == QStringLiteral("invalid_credentials")) {
    return fallbackMessage.isEmpty()
               ? QStringLiteral("아이디 또는 비밀번호가 올바르지 않습니다.")
               : fallbackMessage;
  }
  if (code == QStringLiteral("invalid_otp")) {
    return fallbackMessage.isEmpty()
               ? QStringLiteral("OTP 코드가 올바르지 않습니다.")
               : fallbackMessage;
  }
  if (code == QStringLiteral("service_unavailable")) {
    return fallbackMessage.isEmpty()
               ? QStringLiteral("인증 서버에 연결할 수 없습니다.")
               : fallbackMessage;
  }
  if (code == QStringLiteral("token_expired") ||
      code == QStringLiteral("unauthorized")) {
    return fallbackMessage.isEmpty()
               ? QStringLiteral("로그인 세션이 만료되었습니다. 다시 로그인하세요.")
               : fallbackMessage;
  }
  if (code == QStringLiteral("protocol_error")) {
    return fallbackMessage.isEmpty()
               ? QStringLiteral("인증 서버 응답이 올바르지 않습니다.")
               : fallbackMessage;
  }
  return fallbackMessage.isEmpty()
             ? QStringLiteral("알 수 없는 인증 오류가 발생했습니다.")
             : fallbackMessage;
}

void LoginPage::updateLeftImage() {
  updateLoginPageLeftImage(leftImageLabel_);
}

void LoginPage::closeEvent(QCloseEvent *event) {
  if (authClient_ && !loginSucceeded_) {
    authClient_->cancel();
  }
  if (!loginSucceeded_) {
    emit loginClosed();
  }
  QWidget::closeEvent(event);
}
