#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/cameramanager.h"
#include "core/metadatasynchronizer.h"
#include "core/roirepository.h"
#include "ocr/plateocrcoordinator.h"
#include "ui/video/videowidget.h"
#include <QComboBox>
#include <QCloseEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QTextEdit>
#include <QVector>
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
  void refreshRoiSelector();
  bool isValidRoiName(const QString &name, QString *errorMessage) const;
  bool isDuplicateRoiName(const QString &name) const;

  // Core Managers
  CameraManager *m_cameraManager = nullptr;
  PlateOcrCoordinator *m_ocrCoordinator = nullptr;
  MetadataSynchronizer m_metadataSynchronizer;
  RoiRepository m_roiRepository;

  // UI Components
  VideoWidget *m_videoWidget = nullptr;
  QLineEdit *m_roiNameEdit = nullptr;
  QComboBox *m_roiPurposeCombo = nullptr;
  QComboBox *m_roiSelectorCombo = nullptr;
  QTextEdit *m_logView = nullptr;

  int m_roiSequence = 0;
  QVector<QJsonObject> m_roiRecords;
  QString m_lastCameraLogMessage;
  qint64 m_lastCameraLogMs = 0;
  int m_suppressedCameraLogCount = 0;
};

#endif // MAINWINDOW_H
