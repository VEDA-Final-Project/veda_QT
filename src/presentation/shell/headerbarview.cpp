#include "headerbarview.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include "presentation/controllers/notificationcontroller.h"
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QDialog>
#include <QListWidget>
#include <QVBoxLayout>

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
      PROJECT_SOURCE_DIR "/src/ui/icon/option.png", QColor("#94A3B8")));
  m_ui.settingsButton->setIconSize(QSize(18, 18));
  m_ui.settingsButton->setObjectName("navBtn");
  m_ui.settingsButton->setCursor(Qt::PointingHandCursor);
  m_ui.settingsButton->setToolTip(QString::fromUtf8("로그 설정"));
  headerLayout->addWidget(m_ui.settingsButton);
  headerLayout->addSpacing(2);

  headerLayout->addStretch();

  QPushButton *btnAlarm = new QPushButton(this);
  btnAlarm->setIcon(tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/alarm.png", QColor("#94A3B8")));
  btnAlarm->setIconSize(QSize(18, 18));
  btnAlarm->setObjectName("btnWindowCtrl");
  btnAlarm->setFixedSize(32, 32);
  btnAlarm->setToolTip(QString::fromUtf8("알람 로그"));
  btnAlarm->setCursor(Qt::PointingHandCursor);
  
  connect(btnAlarm, &QPushButton::clicked, this, [this]() {
    QDialog *logDialog = new QDialog(this);
    logDialog->setWindowTitle(QString::fromUtf8("알람 로그"));
    logDialog->resize(450, 300);
    logDialog->setStyleSheet("QDialog { background-color: #1e1e2d; }");
    QVBoxLayout *l = new QVBoxLayout(logDialog);
    QListWidget *list = new QListWidget(logDialog);
    list->setStyleSheet("QListWidget { background-color: #1e1e2d; color: #e2e8f0; border: 1px solid #334155; font-size: 13px; padding: 4px; }");
    QStringList history = NotificationController::getHistory();
    if (history.isEmpty()) {
      list->addItem(QString::fromUtf8("알람 로그가 없습니다."));
    } else {
      list->addItems(history);
    }
    l->addWidget(list);
    QPushButton *btnClose = new QPushButton(QString::fromUtf8("닫기"), logDialog);
    btnClose->setStyleSheet("QPushButton { background: #334155; color: white; padding: 6px; border-radius: 4px; border: none; } QPushButton:hover { background: #475569; }");
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, logDialog, &QDialog::accept);
    l->addWidget(btnClose);
    logDialog->setAttribute(Qt::WA_DeleteOnClose);
    logDialog->show();
  });
  headerLayout->addWidget(btnAlarm);

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
