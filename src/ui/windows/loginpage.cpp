#include "loginpage.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

LoginPage::LoginPage(QWidget *parent)
    : QWidget(parent)
{
  buildUi();
  setWindowTitle(QStringLiteral("회원가입"));
  resize(920, 600);
}

void LoginPage::buildUi()
{
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

  auto *titleLabel = new QLabel(QStringLiteral("회원가입"), rightPanel);
  titleLabel->setObjectName("titleLabel");
  titleLabel->setAlignment(Qt::AlignHCenter);

  auto *credentialHint =
      new QLabel(QStringLiteral("테스트 계정: admin / 1234"), rightPanel);
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

  auto *optionRow = new QHBoxLayout();
  optionRow->setSpacing(20);
  optionRow->setContentsMargins(0, 10, 0, 10);

  rememberPasswordCheck_ =
      new QCheckBox(QStringLiteral("비밀번호 저장"), formArea);
  rememberPasswordCheck_->setObjectName("optionCheck");
  autoLoginCheck_ = new QCheckBox(QStringLiteral("자동 로그인"), formArea);
  autoLoginCheck_->setObjectName("optionCheck");

  optionRow->addWidget(rememberPasswordCheck_);
  optionRow->addWidget(autoLoginCheck_);
  optionRow->addStretch();

  auto *loginButton = new QPushButton(QStringLiteral("인증하고 시작"), formArea);
  loginButton->setObjectName("loginButton");
  loginButton->setMinimumHeight(56);

  loginStatusLabel_ = new QLabel(formArea);
  loginStatusLabel_->setObjectName("loginStatusLabel");
  loginStatusLabel_->setAlignment(Qt::AlignCenter);
  loginStatusLabel_->setVisible(false);

  formLayout->addWidget(idInput_);
  formLayout->addWidget(passwordInput_);
  formLayout->addLayout(optionRow);
  formLayout->addWidget(loginButton);
  formLayout->addWidget(loginStatusLabel_);

  connect(loginButton, &QPushButton::clicked, this, &LoginPage::handleLogin);
  connect(idInput_, &QLineEdit::returnPressed, this, &LoginPage::handleLogin);
  connect(passwordInput_, &QLineEdit::returnPressed, this,
          &LoginPage::handleLogin);

  rightLayout->addStretch();
  rightLayout->addWidget(titleLabel);
  rightLayout->addWidget(credentialHint);
  rightLayout->addSpacing(4);
  rightLayout->addWidget(formArea, 0, Qt::AlignHCenter);
  rightLayout->addStretch();

  rootLayout->addWidget(leftPanel);
  rootLayout->addWidget(rightPanel, 1);
}

void LoginPage::handleLogin()
{
  if (!idInput_ || !passwordInput_ || !loginStatusLabel_)
  {
    return;
  }

  const bool isValid =
      (idInput_->text().trimmed() == QStringLiteral("admin") &&
       passwordInput_->text() == QStringLiteral("1234"));

  loginStatusLabel_->setVisible(true);
  if (isValid)
  {
    loginSucceeded_ = true;
    loginStatusLabel_->setText(QStringLiteral("인증 성공"));
    loginStatusLabel_->setStyleSheet(
        QStringLiteral("color: #10B981; font-size: 14px; font-weight: 700;"));
    emit loginSucceeded();
  }
  else
  {
    loginStatusLabel_->setText(QStringLiteral("아이디 또는 비밀번호가 올바르지 않습니다."));
    loginStatusLabel_->setStyleSheet(
        QStringLiteral("color: #EF4444; font-size: 14px; font-weight: 700;"));
  }
}

void LoginPage::closeEvent(QCloseEvent *event)
{
  if (!loginSucceeded_)
  {
    emit loginClosed();
  }
  QWidget::closeEvent(event);
}
