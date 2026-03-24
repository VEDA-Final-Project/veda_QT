#include "toastoverlay.h"

#include <QEvent>
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kToastWidthPx = 320;
constexpr int kToastAnimationMs = 180;
constexpr int kToastLifetimeMs = 3000;
constexpr int kToastMaxVisible = 3;

QString levelName(ToastOverlay::Level level) {
  switch (level) {
  case ToastOverlay::Level::Success:
    return QStringLiteral("success");
  case ToastOverlay::Level::Warning:
    return QStringLiteral("warning");
  }
  return QStringLiteral("success");
}

class ToastCard final : public QFrame {
public:
  explicit ToastCard(const QString &message, ToastOverlay::Level level,
                     QWidget *parent = nullptr)
      : QFrame(parent) {
    setObjectName(QStringLiteral("toastCard"));
    setProperty("toastLevel", levelName(level));
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
    setFixedWidth(kToastWidthPx);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(0);

    auto *messageLabel = new QLabel(message, this);
    messageLabel->setObjectName(QStringLiteral("toastMessage"));
    messageLabel->setWordWrap(true);
    layout->addWidget(messageLabel);
  }
};

void animateToastIn(QWidget *toast) {
  if (!toast) {
    return;
  }

  auto *effect = new QGraphicsOpacityEffect(toast);
  effect->setOpacity(0.0);
  toast->setGraphicsEffect(effect);
  toast->setMaximumHeight(0);

  toast->ensurePolished();
  toast->updateGeometry();
  const int targetHeight = qMax(56, toast->sizeHint().height());

  auto *group = new QParallelAnimationGroup(toast);

  auto *heightAnimation = new QPropertyAnimation(toast, "maximumHeight", group);
  heightAnimation->setDuration(kToastAnimationMs);
  heightAnimation->setStartValue(0);
  heightAnimation->setEndValue(targetHeight);
  heightAnimation->setEasingCurve(QEasingCurve::OutCubic);

  auto *opacityAnimation = new QPropertyAnimation(effect, "opacity", group);
  opacityAnimation->setDuration(kToastAnimationMs);
  opacityAnimation->setStartValue(0.0);
  opacityAnimation->setEndValue(1.0);
  opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);

  QObject::connect(group, &QParallelAnimationGroup::finished, toast, [toast]() {
    toast->setMaximumHeight(QWIDGETSIZE_MAX);
  });
  group->start(QAbstractAnimation::DeleteWhenStopped);
}
} // namespace

ToastOverlay::ToastOverlay(QWidget *hostWidget)
    : QWidget(hostWidget), m_hostWidget(hostWidget) {
  setObjectName(QStringLiteral("toastOverlay"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_NoSystemBackground);
  setAttribute(Qt::WA_StyledBackground, false);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 16);
  layout->setSpacing(8);
  layout->setAlignment(Qt::AlignTop | Qt::AlignRight);
  m_layout = layout;

  if (m_hostWidget) {
    m_hostWidget->installEventFilter(this);
    syncToHost();
  }
  hide();
}

void ToastOverlay::showToast(const QString &message, Level level) {
  if (!m_hostWidget) {
    return;
  }

  const QString trimmed = message.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }

  syncToHost();
  show();
  raise();

  auto *toast = new ToastCard(trimmed, level, this);
  m_layout->insertWidget(0, toast, 0, Qt::AlignRight);
  animateToastIn(toast);

  QTimer::singleShot(kToastLifetimeMs, toast, [this, toast]() {
    dismissToast(toast);
  });

  while (m_layout->count() > kToastMaxVisible) {
    QWidget *oldest = m_layout->itemAt(m_layout->count() - 1)->widget();
    if (!oldest) {
      break;
    }
    dismissToast(oldest);
  }
}

bool ToastOverlay::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_hostWidget) {
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
      syncToHost();
      break;
    default:
      break;
    }
  }

  return QWidget::eventFilter(watched, event);
}

void ToastOverlay::syncToHost() {
  if (!m_hostWidget) {
    return;
  }

  setGeometry(m_hostWidget->rect());
  raise();
}

void ToastOverlay::dismissToast(QWidget *toast) {
  if (!toast || toast->property("toastClosing").toBool()) {
    return;
  }

  toast->setProperty("toastClosing", true);

  auto *effect =
      qobject_cast<QGraphicsOpacityEffect *>(toast->graphicsEffect());
  if (!effect) {
    effect = new QGraphicsOpacityEffect(toast);
    effect->setOpacity(1.0);
    toast->setGraphicsEffect(effect);
  }

  const int startHeight = qMax(1, toast->height());

  auto *group = new QParallelAnimationGroup(toast);

  auto *heightAnimation = new QPropertyAnimation(toast, "maximumHeight", group);
  heightAnimation->setDuration(kToastAnimationMs);
  heightAnimation->setStartValue(startHeight);
  heightAnimation->setEndValue(0);
  heightAnimation->setEasingCurve(QEasingCurve::InCubic);

  auto *opacityAnimation = new QPropertyAnimation(effect, "opacity", group);
  opacityAnimation->setDuration(kToastAnimationMs);
  opacityAnimation->setStartValue(effect->opacity());
  opacityAnimation->setEndValue(0.0);
  opacityAnimation->setEasingCurve(QEasingCurve::InCubic);

  connect(group, &QParallelAnimationGroup::finished, this, [this, toast]() {
    m_layout->removeWidget(toast);
    toast->deleteLater();
    if (m_layout->count() == 0) {
      hide();
    }
  });
  group->start(QAbstractAnimation::DeleteWhenStopped);
}
