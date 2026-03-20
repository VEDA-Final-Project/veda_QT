#include "headerbarview.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>

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
} // namespace

HeaderBarView::HeaderBarView(QWidget *parent) : QFrame(parent) { setupUi(); }

const HeaderUiRefs &HeaderBarView::uiRefs() const { return m_ui; }

void HeaderBarView::setupUi() {
  setObjectName("headerFrame");
  setFixedHeight(52);

  QHBoxLayout *headerLayout = new QHBoxLayout(this);
  headerLayout->setContentsMargins(12, 6, 12, 6);
  headerLayout->setSpacing(0);

  m_ui.headerIconLabel = new QLabel(this);
  m_ui.headerIconLabel->setObjectName("headerIcon");
  m_ui.headerIconLabel->setText(QString::fromUtf8("\xF0\x9F\x93\xA1"));
  m_ui.headerIconLabel->setFixedSize(32, 32);
  m_ui.headerIconLabel->setAlignment(Qt::AlignCenter);

  m_ui.headerTitleLabel = new QLabel("Veda CCTV Dashboard", this);
  m_ui.headerTitleLabel->setObjectName("headerTitle");

  headerLayout->addWidget(m_ui.headerIconLabel);
  headerLayout->addSpacing(8);
  headerLayout->addWidget(m_ui.headerTitleLabel);
  headerLayout->addSpacing(24);

  m_ui.menuButton = new QToolButton(this);
  m_ui.menuButton->setIcon(
      tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/menu.png", QColor("#94A3B8")));
  m_ui.menuButton->setIconSize(QSize(18, 18));
  m_ui.menuButton->setObjectName("navBtn");
  m_ui.menuButton->setPopupMode(QToolButton::InstantPopup);
  m_ui.menuButton->setCursor(Qt::PointingHandCursor);

  m_ui.navMenu = new QMenu(this);
  m_ui.navMenu->setObjectName("navMenu");
  m_ui.menuButton->setMenu(m_ui.navMenu);
  headerLayout->addWidget(m_ui.menuButton);
  headerLayout->addSpacing(2);

  m_ui.settingsButton = new QToolButton(this);
  m_ui.settingsButton->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/settings.png", QColor("#94A3B8")));
  m_ui.settingsButton->setIconSize(QSize(18, 18));
  m_ui.settingsButton->setObjectName("navBtn");
  m_ui.settingsButton->setCursor(Qt::PointingHandCursor);
  m_ui.settingsButton->setToolTip(QString::fromUtf8("로그 설정"));
  headerLayout->addWidget(m_ui.settingsButton);
  headerLayout->addSpacing(2);

  headerLayout->addStretch();

  m_ui.btnMinimize = new QPushButton(this);
  m_ui.btnMinimize->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/minimize.png", QColor("#94A3B8")));
  m_ui.btnMinimize->setIconSize(QSize(18, 18));
  m_ui.btnMinimize->setObjectName("btnWindowCtrl");
  m_ui.btnMinimize->setFixedSize(32, 32);
  m_ui.btnMinimize->setToolTip(QString::fromUtf8("최소화"));
  headerLayout->addWidget(m_ui.btnMinimize);

  m_ui.btnMaxRestore = new QPushButton(this);
  m_ui.btnMaxRestore->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/maximize.png", QColor("#94A3B8")));
  m_ui.btnMaxRestore->setIconSize(QSize(18, 18));
  m_ui.btnMaxRestore->setObjectName("btnWindowCtrl");
  m_ui.btnMaxRestore->setFixedSize(32, 32);
  m_ui.btnMaxRestore->setToolTip(QString::fromUtf8("최대화"));
  headerLayout->addWidget(m_ui.btnMaxRestore);

  m_ui.btnExit = new QPushButton(this);
  m_ui.btnExit->setIcon(
      tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/exit.png", QColor("#94A3B8")));
  m_ui.btnExit->setIconSize(QSize(18, 18));
  m_ui.btnExit->setObjectName("btnClose");
  m_ui.btnExit->setFixedSize(32, 32);
  m_ui.btnExit->setToolTip(QString::fromUtf8("종료"));
  headerLayout->addWidget(m_ui.btnExit);
}
