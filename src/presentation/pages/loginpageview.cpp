#include "loginpageview.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QIcon tintIcon(const QString &path, const QColor &color) {
  QPixmap pixmap(path);
  if (pixmap.isNull()) {
    return QIcon();
  }

  QPixmap result(pixmap.size());
  result.fill(Qt::transparent);

  QPainter painter(&result);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.drawPixmap(0, 0, pixmap);
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(result.rect(), color);
  painter.end();

  return QIcon(result);
}

QString loginPageStyleSheet() {
  return QStringLiteral(
      "#loginPage { background: #0f172a; }"
      "#leftPanel { background: #18323a; border-right: 1px solid #2c4450; }"
      "#leftImage { background: transparent; }"
      "#rightPanel { background: #0f172a; }"
      "#loginTopBar { background: transparent; }"
      "#titleLabel { color: #f8fafc; font-size: 32px; font-weight: 800; letter-spacing: 1px; }"
      "#credentialHint { color: #94a3b8; font-size: 11px; }"
      "#otpHintLabel { color: #cbd5e1; font-size: 11px; }"
      "#formArea { background: transparent; }"
      "#idInput, #passwordInput, #otpInput {"
      "  background: #1e293b;"
      "  color: #f8fafc;"
      "  border: 1px solid #334155;"
      "  border-radius: 8px;"
      "  padding: 0 10px;"
      "  font-size: 12px;"
      "}"
      "#idInput:focus, #passwordInput:focus, #otpInput:focus {"
      "  border: 1px solid #34d399;"
      "}"
      "#loginButton {"
      "  background: #22c55e;"
      "  color: white;"
      "  border: none;"
      "  border-radius: 8px;"
      "  font-size: 15px;"
      "  font-weight: 700;"
      "}"
      "#loginButton:hover { background: #34d399; }"
      "#loginButton:pressed { background: #16a34a; }"
      "#loginButton:disabled { background: #475569; color: #cbd5e1; }"
      "#secondaryButton {"
      "  background: transparent;"
      "  color: #cbd5e1;"
      "  border: 1px solid #475569;"
      "  border-radius: 8px;"
      "  font-size: 13px;"
      "  font-weight: 600;"
      "}"
      "#secondaryButton:hover { background: #1e293b; }"
      "#secondaryButton:disabled { color: #64748b; border-color: #334155; }"
      "#loginCloseButton {"
      "  background: transparent;"
      "  border: none;"
      "  border-radius: 10px;"
      "}"
      "#loginCloseButton:hover { background: #1e293b; }"
      "#loginCloseButton:pressed { background: #334155; }"
      "#loginStatusLabel { font-size: 11px; font-weight: 700; }");
}
} // namespace

LoginPageUiRefs buildLoginPageUi(QWidget *page) {
  LoginPageUiRefs ui;

  page->setObjectName(QStringLiteral("loginPage"));
  page->setStyleSheet(loginPageStyleSheet());

  auto *rootLayout = new QHBoxLayout(page);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto *leftPanel = new QFrame(page);
  leftPanel->setObjectName(QStringLiteral("leftPanel"));
  leftPanel->setFixedWidth(400);

  auto *leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(36, 36, 36, 36);
  leftLayout->setSpacing(0);
  leftLayout->addStretch();

  ui.leftImageLabel = new QLabel(leftPanel);
  ui.leftImageLabel->setObjectName(QStringLiteral("leftImage"));
  ui.leftImageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  ui.leftImageLabel->setAlignment(Qt::AlignCenter);
  ui.leftImageLabel->setText(QString());
  ui.leftImageLabel->setStyleSheet(
      QStringLiteral("background: transparent; border: none;"));
  leftLayout->addWidget(ui.leftImageLabel, 0, Qt::AlignCenter);
  leftLayout->addStretch();

  auto *rightPanel = new QFrame(page);
  rightPanel->setObjectName(QStringLiteral("rightPanel"));

  auto *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(48, 24, 48, 24);
  rightLayout->setSpacing(12);

  auto *topBar = new QWidget(rightPanel);
  topBar->setObjectName(QStringLiteral("loginTopBar"));
  auto *topBarLayout = new QHBoxLayout(topBar);
  topBarLayout->setContentsMargins(0, 0, 0, 0);
  topBarLayout->setSpacing(0);
  topBarLayout->addStretch();

  ui.closeButton = new QPushButton(topBar);
  ui.closeButton->setObjectName(QStringLiteral("loginCloseButton"));
  ui.closeButton->setFixedSize(40, 40);
  ui.closeButton->setCursor(Qt::PointingHandCursor);
  ui.closeButton->setToolTip(QStringLiteral("닫기"));
  ui.closeButton->setIcon(
      tintIcon(QStringLiteral(PROJECT_SOURCE_DIR "/src/ui/icon/exit.png"),
               QColor(QStringLiteral("#94A3B8"))));
  ui.closeButton->setIconSize(QSize(18, 18));
  topBarLayout->addWidget(ui.closeButton);

  auto *titleLabel = new QLabel(QStringLiteral("로그인"), rightPanel);
  titleLabel->setObjectName(QStringLiteral("titleLabel"));
  titleLabel->setAlignment(Qt::AlignHCenter);

  auto *credentialHint =
      new QLabel(QStringLiteral("Raspberry Pi 인증 서버와 OTP 앱으로 인증합니다."),
                 rightPanel);
  credentialHint->setObjectName(QStringLiteral("credentialHint"));
  credentialHint->setAlignment(Qt::AlignHCenter);

  auto *formArea = new QWidget(rightPanel);
  formArea->setObjectName(QStringLiteral("formArea"));
  formArea->setFixedWidth(220);

  auto *formLayout = new QVBoxLayout(formArea);
  formLayout->setContentsMargins(0, 0, 0, 0);
  formLayout->setSpacing(8);

  ui.idInput = new QLineEdit(formArea);
  ui.idInput->setObjectName(QStringLiteral("idInput"));
  ui.idInput->setMinimumHeight(36);
  ui.idInput->setPlaceholderText(QStringLiteral("아이디"));

  ui.passwordInput = new QLineEdit(formArea);
  ui.passwordInput->setObjectName(QStringLiteral("passwordInput"));
  ui.passwordInput->setMinimumHeight(36);
  ui.passwordInput->setEchoMode(QLineEdit::Password);
  ui.passwordInput->setPlaceholderText(QStringLiteral("비밀번호"));

  ui.otpHintLabel = new QLabel(formArea);
  ui.otpHintLabel->setObjectName(QStringLiteral("otpHintLabel"));
  ui.otpHintLabel->setAlignment(Qt::AlignCenter);
  ui.otpHintLabel->setWordWrap(true);

  ui.otpInput = new QLineEdit(formArea);
  ui.otpInput->setObjectName(QStringLiteral("otpInput"));
  ui.otpInput->setMinimumHeight(36);
  ui.otpInput->setPlaceholderText(QStringLiteral("OTP (6자리)"));
  ui.otpInput->setMaxLength(6);
  ui.otpInput->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("\\d{0,6}")), ui.otpInput));

  ui.loginButton = new QPushButton(QStringLiteral("인증하고 시작"), formArea);
  ui.loginButton->setObjectName(QStringLiteral("loginButton"));
  ui.loginButton->setMinimumHeight(38);

  ui.backButton = new QPushButton(QStringLiteral("뒤로가기"), formArea);
  ui.backButton->setObjectName(QStringLiteral("secondaryButton"));
  ui.backButton->setMinimumHeight(38);

  ui.loginStatusLabel = new QLabel(formArea);
  ui.loginStatusLabel->setObjectName(QStringLiteral("loginStatusLabel"));
  ui.loginStatusLabel->setAlignment(Qt::AlignCenter);
  ui.loginStatusLabel->setWordWrap(true);
  ui.loginStatusLabel->setVisible(false);

  auto *buttonRow = new QHBoxLayout();
  buttonRow->setContentsMargins(0, 0, 0, 0);
  buttonRow->setSpacing(8);
  buttonRow->addWidget(ui.backButton);
  buttonRow->addWidget(ui.loginButton, 1);

  formLayout->addWidget(ui.idInput);
  formLayout->addWidget(ui.passwordInput);
  formLayout->addWidget(ui.otpHintLabel);
  formLayout->addWidget(ui.otpInput);
  formLayout->addLayout(buttonRow);
  formLayout->addWidget(ui.loginStatusLabel);

  rightLayout->addWidget(topBar, 0, Qt::AlignRight);
  rightLayout->addStretch();
  rightLayout->addWidget(titleLabel);
  rightLayout->addWidget(credentialHint);
  rightLayout->addSpacing(6);
  rightLayout->addWidget(formArea, 0, Qt::AlignHCenter);
  rightLayout->addStretch();

  rootLayout->addWidget(leftPanel);
  rootLayout->addWidget(rightPanel, 1);

  return ui;
}

void updateLoginPageLeftImage(QLabel *leftImageLabel) {
  if (!leftImageLabel) {
    return;
  }

  const QPixmap source(QStringLiteral(PROJECT_SOURCE_DIR "/src/ui/icon/road.png"));
  if (source.isNull()) {
    leftImageLabel->clear();
    return;
  }

  const QPixmap croppedSource =
      (source.width() > 4 && source.height() > 4)
          ? source.copy(2, 2, source.width() - 4, source.height() - 4)
          : source;

  const QWidget *panel = leftImageLabel->parentWidget();
  if (!panel) {
    return;
  }

  const int targetWidth = qMax(180, qMin(panel->width() - 72, 300));
  const QSize targetSize(targetWidth, static_cast<int>(targetWidth / 1.785));

  leftImageLabel->setPixmap(
      croppedSource.scaled(targetSize, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation));
}
