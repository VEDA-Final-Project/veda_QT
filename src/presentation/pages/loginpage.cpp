#include "loginpage.h"

#include "config/config.h"
#include "infrastructure/rpi/rpiauthclient.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSizePolicy>
#include <QVBoxLayout>

LoginPage::LoginPage(QWidget *parent) : QWidget(parent) {
  authClient_ = new RpiAuthClient(this);
  authClient_->setTimeouts(Config::instance().authConnectTimeoutMs(),
                           Config::instance().authRequestTimeoutMs());
  buildUi();
  connect(authClient_, &RpiAuthClient::loginStepFinished, this,
          &LoginPage::handleLoginStepFinished);
  connect(authClient_, &RpiAuthClient::authFinished, this,
          &LoginPage::handleAuthFinished);
  setWindowTitle(QStringLiteral("로그인"));
  resize(920, 600);
}

void LoginPage::buildUi() {
  setObjectName("loginPage");

  auto *rootLayout = new QHBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto *leftPanel = new QFrame(this);
  leftPanel->setObjectName("leftPanel");
  leftPanel->setFixedWidth(350);

  auto *leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(0);

  auto *leftImage = new QLabel(leftPanel);
  leftImage->setObjectName("leftImage");
  leftImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  leftImage->setAlignment(Qt::AlignCenter);
  leftImage->setText(QString());
  leftLayout->addWidget(leftImage, 1);

  auto *rightPanel = new QFrame(this);
  rightPanel->setObjectName("rightPanel");

  auto *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(24, 22, 24, 22);
  rightLayout->setSpacing(16);

  auto *titleLabel = new QLabel(QStringLiteral("로그인"), rightPanel);
  titleLabel->setObjectName("titleLabel");
  titleLabel->setAlignment(Qt::AlignHCenter);

  auto *credentialHint = new QLabel(
      QStringLiteral("Raspberry Pi 인증 서버와 OTP 앱으로 인증합니다."),
      rightPanel);
  credentialHint->setObjectName("credentialHint");
  credentialHint->setAlignment(Qt::AlignHCenter);

  auto *formArea = new QWidget(rightPanel);
  formArea->setObjectName("formArea");
  formArea->setFixedWidth(430);

  auto *formLayout = new QVBoxLayout(formArea);
  formLayout->setContentsMargins(0, 0, 0, 0);
  formLayout->setSpacing(16);

  idInput_ = new QLineEdit(formArea);
  idInput_->setObjectName("idInput");
  idInput_->setMinimumHeight(54);
  idInput_->setPlaceholderText(QStringLiteral("아이디"));

  passwordInput_ = new QLineEdit(formArea);
  passwordInput_->setObjectName("passwordInput");
  passwordInput_->setMinimumHeight(54);
  passwordInput_->setEchoMode(QLineEdit::Password);
  passwordInput_->setPlaceholderText(QStringLiteral("비밀번호"));

  otpHintLabel_ = new QLabel(formArea);
  otpHintLabel_->setObjectName("otpHintLabel");
  otpHintLabel_->setAlignment(Qt::AlignCenter);
  otpHintLabel_->setWordWrap(true);

  otpInput_ = new QLineEdit(formArea);
  otpInput_->setObjectName("otpInput");
  otpInput_->setMinimumHeight(54);
  otpInput_->setPlaceholderText(QStringLiteral("OTP (6자리)"));
  otpInput_->setMaxLength(6);
  otpInput_->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("\\d{0,6}")), otpInput_));

  loginButton_ = new QPushButton(QStringLiteral("인증하고 시작"), formArea);
  loginButton_->setObjectName("loginButton");
  loginButton_->setMinimumHeight(56);

  backButton_ = new QPushButton(QStringLiteral("뒤로가기"), formArea);
  backButton_->setObjectName("secondaryButton");
  backButton_->setMinimumHeight(56);

  loginStatusLabel_ = new QLabel(formArea);
  loginStatusLabel_->setObjectName("loginStatusLabel");
  loginStatusLabel_->setAlignment(Qt::AlignCenter);
  loginStatusLabel_->setVisible(false);

  auto *buttonRow = new QHBoxLayout();
  buttonRow->setContentsMargins(0, 0, 0, 0);
  buttonRow->setSpacing(12);
  buttonRow->addWidget(backButton_);
  buttonRow->addWidget(loginButton_, 1);

  formLayout->addWidget(idInput_);
  formLayout->addWidget(passwordInput_);
  formLayout->addWidget(otpHintLabel_);
  formLayout->addWidget(otpInput_);
  formLayout->addLayout(buttonRow);
  formLayout->addWidget(loginStatusLabel_);

  connect(loginButton_, &QPushButton::clicked, this, &LoginPage::handleLogin);
  connect(backButton_, &QPushButton::clicked, this,
          &LoginPage::showCredentialsStep);
  connect(idInput_, &QLineEdit::returnPressed, this, &LoginPage::handleLogin);
  connect(passwordInput_, &QLineEdit::returnPressed, this,
          &LoginPage::handleLogin);
  connect(otpInput_, &QLineEdit::returnPressed, this, &LoginPage::handleLogin);

  rightLayout->addStretch();
  rightLayout->addWidget(titleLabel);
  rightLayout->addWidget(credentialHint);
  rightLayout->addSpacing(4);
  rightLayout->addWidget(formArea, 0, Qt::AlignHCenter);
  rightLayout->addStretch();

  rootLayout->addWidget(leftPanel);
  rootLayout->addWidget(rightPanel, 1);

  updateStepUi();
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
    setLoginUiBusy(false);
    loginSucceeded_ = true;
    showStatusMessage(message.isEmpty() ? QStringLiteral("개발 모드: OTP 생략")
                                        : message,
                      true);
    emit loginSucceeded();
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
          ? QStringLiteral("color: #10B981; font-size: 14px; font-weight: 700;")
          : QStringLiteral(
                "color: #EF4444; font-size: 14px; font-weight: 700;"));
}

void LoginPage::showProgressMessage(const QString &message) {
  if (!loginStatusLabel_) {
    return;
  }

  loginStatusLabel_->setVisible(true);
  loginStatusLabel_->setText(message);
  loginStatusLabel_->setStyleSheet(
      QStringLiteral("color: #CBD5E1; font-size: 14px; font-weight: 700;"));
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

void LoginPage::closeEvent(QCloseEvent *event) {
  if (authClient_ && !loginSucceeded_) {
    authClient_->cancel();
  }
  if (!loginSucceeded_) {
    emit loginClosed();
  }
  QWidget::closeEvent(event);
}
