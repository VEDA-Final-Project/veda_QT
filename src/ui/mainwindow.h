#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cameramanager.h"
#include "metadatasynchronizer.h"
#include "plateocrcoordinator.h"
#include "videowidget.h"
#include <QCloseEvent>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QTextEdit>
#include <QtGlobal>

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
  // OCR Result from PlateOcrCoordinator
  void onOcrResult(int objectId, const QString &result);

private:
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void flushSuppressedCameraLogs();

  // Core Managers
  CameraManager *m_cameraManager = nullptr;
  PlateOcrCoordinator *m_ocrCoordinator = nullptr;
  MetadataSynchronizer m_metadataSynchronizer;

  // UI Components
  VideoWidget *m_videoWidget = nullptr;
  QTextEdit *m_logView = nullptr;

  int m_roiSequence = 0;
  QString m_lastCameraLogMessage;
  qint64 m_lastCameraLogMs = 0;
  int m_suppressedCameraLogCount = 0;
};

#endif // MAINWINDOW_H
