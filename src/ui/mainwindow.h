#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cameramanager.h"
#include "videowidget.h"
#include <QCloseEvent>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QTextEdit>

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void playCctv();

  // Log from CameraManager
  void onLogMessage(const QString &msg);
  // OCR Result from VideoWidget
  void onOcrResult(int objectId, const QString &result);

private:
  void appendRoiStructuredLog(const QJsonObject &roiData);

  // Core Managers
  CameraManager *m_cameraManager = nullptr;

  // UI Components
  VideoWidget *m_videoWidget = nullptr;
  QTextEdit *m_logView = nullptr;

  int m_roiSequence = 0;
};

#endif // MAINWINDOW_H
