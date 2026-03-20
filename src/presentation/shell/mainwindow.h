#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "presentation/shell/mainwindowuirefs.h"
#include <QMainWindow>
#include <QPoint>
#include <QStackedWidget>

class MainWindowController;
class ControllerDialog;
class HeaderBarView;
class CctvSplashPageView;
class CctvDashboardView;
class TelegramPageView;
class DbPageView;
class RecordPageView;
class QCloseEvent;
class QEvent;
class QMenu;
class QMouseEvent;
class QTimer;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override = default;
  MainWindowUiRefs controllerUiRefs() const;
  void attachController(MainWindowController *controller);
  void showCctvSplash(const QString &message = QString());
  void showCctvPage();
  static constexpr int kDbPageIndex = 3;

protected:
  void closeEvent(QCloseEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
#ifdef Q_OS_WIN
  bool nativeEvent(const QByteArray &eventType, void *message,
                   qintptr *result) override;
#endif

public slots:
  void navigateToPage(int stackedIndex);
  void navigateToDbSubTab(int tabIndex);

private:
  static constexpr int kSplashPageIndex = 0;
  static constexpr int kCctvPageIndex = 1;

  void setupUi();
  void openLogFilterSettings();

  HeaderBarView *m_headerView = nullptr;
  CctvSplashPageView *m_splashView = nullptr;
  CctvDashboardView *m_cctvView = nullptr;
  TelegramPageView *m_telegramView = nullptr;
  DbPageView *m_dbView = nullptr;
  RecordPageView *m_recordView = nullptr;

  QTimer *m_clockTimer = nullptr;
  QStackedWidget *m_stackedWidget = nullptr;
  bool m_isCctvReady = false;
  QPoint m_dragPosition;

  MainWindowController *m_controller = nullptr;
  ControllerDialog *m_controllerDialog = nullptr;
};

#endif // MAINWINDOW_H
