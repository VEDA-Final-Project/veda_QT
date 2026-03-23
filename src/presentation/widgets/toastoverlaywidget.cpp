#include "presentation/widgets/toastoverlaywidget.h"

#include <QColor>
#include <QDateTime>
#include <QEasingCurve>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

namespace {
enum class ToastKind {
  RoiCreated,
  RoiDeleted,
  VehicleEntered,
  VehicleDeparted,
  Default
};

constexpr int kToastWidth = 348;
constexpr int kMaxVisibleToasts = 4;
constexpr int kRightMargin = 20;
constexpr int kTopMargin = 72;
constexpr int kBottomMargin = 10;
constexpr int kToastDurationMs = 3200;
constexpr int kAnimationMs = 220;

ToastKind toastKindForTitle(const QString &title) {
  if (title == QStringLiteral("ROI 생성")) {
    return ToastKind::RoiCreated;
  }
  if (title == QStringLiteral("ROI 삭제")) {
    return ToastKind::RoiDeleted;
  }
  if (title == QStringLiteral("입차")) {
    return ToastKind::VehicleEntered;
  }
  if (title == QStringLiteral("출차")) {
    return ToastKind::VehicleDeparted;
  }
  return ToastKind::Default;
}

QString accentColor(ToastKind kind) {
  switch (kind) {
  case ToastKind::RoiCreated:
    return QStringLiteral("#38BDF8");
  case ToastKind::RoiDeleted:
    return QStringLiteral("#F59E0B");
  case ToastKind::VehicleEntered:
    return QStringLiteral("#10B981");
  case ToastKind::VehicleDeparted:
    return QStringLiteral("#FB7185");
  case ToastKind::Default:
    return QStringLiteral("#94A3B8");
  }
  return QStringLiteral("#94A3B8");
}

QString badgeText(ToastKind kind) {
  switch (kind) {
  case ToastKind::RoiCreated:
  case ToastKind::RoiDeleted:
    return QStringLiteral("ROI");
  case ToastKind::VehicleEntered:
  case ToastKind::VehicleDeparted:
    return QStringLiteral("PARKING");
  case ToastKind::Default:
    return QStringLiteral("EVENT");
  }
  return QStringLiteral("EVENT");
}

QString titleColor(ToastKind kind) {
  switch (kind) {
  case ToastKind::VehicleEntered:
    return QStringLiteral("#ECFDF5");
  case ToastKind::VehicleDeparted:
    return QStringLiteral("#FFF1F2");
  case ToastKind::RoiCreated:
  case ToastKind::RoiDeleted:
  case ToastKind::Default:
    return QStringLiteral("#F8FAFC");
  }
  return QStringLiteral("#F8FAFC");
}
} // namespace

ToastOverlayWidget::ToastOverlayWidget(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("toastOverlay"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_NoSystemBackground);
  setStyleSheet("#toastOverlay { background: transparent; }");

  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(10, 10, 10, 10);
  m_layout->setSpacing(12);

  hide();
}

void ToastOverlayWidget::showToast(const QString &title, const QString &body) {
  const ToastKind kind = toastKindForTitle(title);
  const QString accent = accentColor(kind);

  auto *card = new QFrame(this);
  card->setObjectName(QStringLiteral("toastCard"));
  card->setAttribute(Qt::WA_TransparentForMouseEvents);
  card->setFixedWidth(kToastWidth);
  card->setMaximumHeight(0);
  const QString cardStyle = QString(
      "#toastCard {"
      "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, "
      "stop:0 rgba(15, 23, 42, 246), "
      "stop:0.6 rgba(15, 23, 42, 236), "
      "stop:1 rgba(30, 41, 59, 226));"
      "border: 1px solid rgba(148, 163, 184, 56);"
      "border-radius: 18px;"
      "}"
      "QWidget#toastContent {"
      "background: transparent;"
      "border: none;"
      "}"
      "QFrame#toastAccent {"
      "background-color: %1;"
      "border-radius: 2px;"
      "}"
      "QLabel#toastBadge {"
      "background-color: rgba(15, 23, 42, 150);"
      "color: %1;"
      "border: 1px solid rgba(148, 163, 184, 42);"
      "border-radius: 9px;"
      "padding: 2px 8px;"
      "font-size: 10px;"
      "font-weight: 700;"
      "letter-spacing: 0.4px;"
      "}"
      "QLabel#toastTitle {"
      "color: %2;"
      "font-size: 15px;"
      "font-weight: 700;"
      "}"
      "QLabel#toastBody {"
      "color: #CBD5E1;"
      "font-size: 12px;"
      "line-height: 1.45;"
      "}"
      "QLabel#toastHint {"
      "color: rgba(148, 163, 184, 176);"
      "font-size: 10px;"
      "font-weight: 500;"
      "}")
                                .arg(accent, titleColor(kind));
  card->setStyleSheet(cardStyle);

  auto *shadow = new QGraphicsDropShadowEffect(card);
  shadow->setBlurRadius(32);
  shadow->setOffset(0, 12);
  shadow->setColor(QColor(8, 15, 29, 150));
  card->setGraphicsEffect(shadow);

  auto *outerLayout = new QHBoxLayout(card);
  outerLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->setSpacing(0);

  auto *accentBar = new QFrame(card);
  accentBar->setObjectName(QStringLiteral("toastAccent"));
  accentBar->setFixedWidth(4);
  outerLayout->addWidget(accentBar);

  auto *content = new QWidget(card);
  content->setObjectName(QStringLiteral("toastContent"));
  content->setAttribute(Qt::WA_StyledBackground, true);
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(16, 14, 16, 14);
  contentLayout->setSpacing(8);
  outerLayout->addWidget(content, 1);

  auto *headerLayout = new QHBoxLayout();
  headerLayout->setContentsMargins(0, 0, 0, 0);
  headerLayout->setSpacing(8);

  auto *badgeLabel = new QLabel(badgeText(kind), content);
  badgeLabel->setObjectName(QStringLiteral("toastBadge"));
  headerLayout->addWidget(badgeLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
  headerLayout->addStretch();
  contentLayout->addLayout(headerLayout);

  auto *titleLabel = new QLabel(title, content);
  titleLabel->setObjectName(QStringLiteral("toastTitle"));
  titleLabel->setWordWrap(true);
  contentLayout->addWidget(titleLabel);

  auto *bodyLabel = new QLabel(body, content);
  bodyLabel->setObjectName(QStringLiteral("toastBody"));
  bodyLabel->setWordWrap(true);
  contentLayout->addWidget(bodyLabel);

  auto *hintLabel =
      new QLabel(QDateTime::currentDateTime().toString("hh:mm:ss"), content);
  hintLabel->setObjectName(QStringLiteral("toastHint"));
  contentLayout->addWidget(hintLabel);

  m_layout->addWidget(card);
  m_cards.append(card);
  trimOverflow();

  const int targetHeight = card->sizeHint().height();
  auto *heightAnim = new QPropertyAnimation(card, "maximumHeight", card);
  heightAnim->setDuration(kAnimationMs);
  heightAnim->setStartValue(0);
  heightAnim->setEndValue(targetHeight);
  heightAnim->setEasingCurve(QEasingCurve::OutCubic);

  resize(overlaySizeHint());
  repositionInParent();
  show();
  raise();
  heightAnim->start(QAbstractAnimation::DeleteWhenStopped);

  QTimer::singleShot(kToastDurationMs, this, [this, card]() { removeToast(card); });
}

void ToastOverlayWidget::repositionInParent() {
  QWidget *parent = parentWidget();
  if (!parent) {
    return;
  }

  const QSize size = overlaySizeHint();
  resize(size);
  const int x = parent->width() - size.width() - kRightMargin;
  move(x > 0 ? x : 0, kTopMargin);
}

void ToastOverlayWidget::removeToast(QFrame *card) {
  if (!card) {
    return;
  }

  if (card->property("closing").toBool()) {
    return;
  }

  const int index = m_cards.indexOf(card);
  if (index < 0) {
    return;
  }

  card->setProperty("closing", true);

  auto *heightAnim = new QPropertyAnimation(card, "maximumHeight", card);
  heightAnim->setDuration(kAnimationMs);
  heightAnim->setStartValue(card->maximumHeight() > 0 ? card->maximumHeight()
                                                      : card->height());
  heightAnim->setEndValue(0);
  heightAnim->setEasingCurve(QEasingCurve::InCubic);

  connect(heightAnim, &QPropertyAnimation::finished, this,
          [this, card]() { finalizeRemoval(card); });
  heightAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastOverlayWidget::finalizeRemoval(QFrame *card) {
  if (!card) {
    return;
  }

  const int index = m_cards.indexOf(card);
  if (index < 0) {
    return;
  }

  m_cards.removeAt(index);
  m_layout->removeWidget(card);
  card->deleteLater();

  if (m_cards.isEmpty()) {
    hide();
  } else {
    resize(overlaySizeHint());
    repositionInParent();
  }
}

void ToastOverlayWidget::trimOverflow() {
  while (m_cards.size() > kMaxVisibleToasts) {
    removeToast(m_cards.front());
  }
}

QSize ToastOverlayWidget::overlaySizeHint() const {
  const QSize layoutSize = m_layout ? m_layout->sizeHint() : QSize(kToastWidth, 0);
  return QSize(kToastWidth, layoutSize.height() + kBottomMargin);
}
