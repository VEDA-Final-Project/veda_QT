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
  setWindowTitle(QStringLiteral("Login"));
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

  auto *titleLabel = new QLabel(QStringLiteral("Login"), rightPanel);
  titleLabel->setObjectName("titleLabel");
  titleLabel->setAlignment(Qt::AlignHCenter);

  auto *credentialHint =
      new QLabel(QStringLiteral("ID: admin / PW: 1234"), rightPanel);
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
  idInput_->setPlaceholderText(QStringLiteral("ID"));

  passwordInput_ = new QLineEdit(formArea);
  passwordInput_->setObjectName("passwordInput");
  passwordInput_->setMinimumHeight(54);
  passwordInput_->setEchoMode(QLineEdit::Password);
  passwordInput_->setPlaceholderText(QStringLiteral("Password"));

  auto *optionRow = new QHBoxLayout();
  optionRow->setSpacing(20);
  optionRow->setContentsMargins(0, 10, 0, 10);

  rememberPasswordCheck_ =
      new QCheckBox(QStringLiteral("Remember Password"), formArea);
  rememberPasswordCheck_->setObjectName("optionCheck");
  autoLoginCheck_ = new QCheckBox(QStringLiteral("Auto Login"), formArea);
  autoLoginCheck_->setObjectName("optionCheck");

  optionRow->addWidget(rememberPasswordCheck_);
  optionRow->addWidget(autoLoginCheck_);
  optionRow->addStretch();

  auto *loginButton = new QPushButton(QStringLiteral("Login"), formArea);
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

  setStyleSheet(QStringLiteral(
      "QWidget {"
      "  font-family: 'HanwhaGothicB', 'Hanwha Gothic B', 'Hanwha Gothic', "
      "'Malgun Gothic';"
      "}"
      "#loginPage { background: #2B2D34; }"
      "#leftPanel { background: #1F2229; }"
      "#leftImage {"
      "  background: #30333B;"
      "  border: 1px solid #3A3E48;"
      "}"
      "#rightPanel { background: #2B2D34; }"
      "#titleLabel {"
      "  color: #F37321;"
      "  font-size: 48px;"
      "  font-weight: 700;"
      "}"
      "#credentialHint {"
      "  color: #A6ACB8;"
      "  font-size: 14px;"
      "  font-weight: 600;"
      "}"
      "#idInput, #passwordInput {"
      "  border: 1px solid #3A3E48;"
      "  border-radius: 2px;"
      "  background: #20242D;"
      "  padding: 0 14px;"
      "  font-size: 16px;"
      "  color: #E7EAF1;"
      "}"
      "#idInput::placeholder, #passwordInput::placeholder {"
      "  color: #89909E;"
      "}"
      "#idInput:focus, #passwordInput:focus {"
      "  border: 1px solid #F37321;"
      "}"
      "QCheckBox#optionCheck {"
      "  color: #DDE1EA;"
      "  font-size: 14px;"
      "  spacing: 10px;"
      "}"
      "QCheckBox#optionCheck::indicator {"
      "  width: 18px;"
      "  height: 18px;"
      "  border: 2px solid #505664;"
      "  background: #20242D;"
      "}"
      "QCheckBox#optionCheck::indicator:checked {"
      "  border: 2px solid #F37321;"
      "  background: #F37321;"
      "}"
      "#loginButton {"
      "  border: none;"
      "  border-radius: 2px;"
      "  background: #F37321;"
      "  color: #101216;"
      "  font-size: 18px;"
      "  font-weight: 600;"
      "}"
      "#loginButton:hover { background: #FF8A42; }"
      "#loginStatusLabel {"
      "  color: #A6ACB8;"
      "  font-size: 14px;"
      "  font-weight: 600;"
      "}"));
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
    loginStatusLabel_->setText(QStringLiteral("Login success"));
    loginStatusLabel_->setStyleSheet(
        QStringLiteral("color: #22C55E; font-size: 14px; font-weight: 700;"));
    emit loginSucceeded();
  }
  else
  {
    loginStatusLabel_->setText(QStringLiteral("Invalid ID or password."));
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
