#include "cctvsplashpageview.h"

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

CctvSplashPageView::CctvSplashPageView(QWidget *parent) : QWidget(parent) {
  setupUi();
}

const SplashUiRefs &CctvSplashPageView::uiRefs() const { return m_ui; }

void CctvSplashPageView::setupUi() {
  setObjectName("cctvSplashPage");

  QVBoxLayout *splashLayout = new QVBoxLayout(this);
  splashLayout->setContentsMargins(24, 24, 24, 24);
  splashLayout->setSpacing(18);
  splashLayout->addStretch();

  QFrame *splashCard = new QFrame(this);
  splashCard->setObjectName("cctvSplashCard");
  splashCard->setMaximumWidth(560);
  QVBoxLayout *splashCardLayout = new QVBoxLayout(splashCard);
  splashCardLayout->setContentsMargins(36, 30, 36, 30);
  splashCardLayout->setSpacing(14);

  m_ui.titleLabel = new QLabel(QString::fromUtf8("CCTV 준비 중"), splashCard);
  m_ui.titleLabel->setObjectName("cctvSplashTitle");
  m_ui.titleLabel->setAlignment(Qt::AlignCenter);

  m_ui.messageLabel =
      new QLabel(QString::fromUtf8("카메라 연결을 확인하고 있습니다."), splashCard);
  m_ui.messageLabel->setObjectName("cctvSplashMessage");
  m_ui.messageLabel->setWordWrap(true);
  m_ui.messageLabel->setAlignment(Qt::AlignCenter);

  QProgressBar *splashProgress = new QProgressBar(splashCard);
  splashProgress->setObjectName("cctvSplashProgress");
  splashProgress->setRange(0, 0);
  splashProgress->setTextVisible(false);
  splashProgress->setFixedHeight(8);

  splashCardLayout->addWidget(m_ui.titleLabel);
  splashCardLayout->addWidget(m_ui.messageLabel);
  splashCardLayout->addWidget(splashProgress);
  splashLayout->addWidget(splashCard, 0, Qt::AlignHCenter);
  splashLayout->addStretch();
}
