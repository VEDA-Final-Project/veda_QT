
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cameramanager.h"
#include "videowidget.h"
#include <QLabel>
#include <QMainWindow>
#include <QCloseEvent>
#include <QTextEdit>

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();


protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void playCctv();
  void decreaseSync(); // -100ms
  void increaseSync(); // +100ms

  // Log from CameraManager
  void onLogMessage(const QString &msg);
  // OCR Result from VideoWidget
  void onOcrResult(int objectId, const QString &result);

private:
  void updateSyncLabel();

  // Core Managers
  CameraManager *m_cameraManager;

  // UI Components
  VideoWidget *m_videoWidget;
  QTextEdit *m_logView;
  QLabel *m_lblSync;

  int m_syncDelayMs; // 메타데이터 표시 지연 시간 (ms)
};
#endif // MAINWINDOW_H



