#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include "core/cameramanager.h"
#include "core/metadatasynchronizer.h"
#include "core/roirepository.h"
#include "ocr/plateocrcoordinator.h"
#include <QComboBox>
#include <QJsonObject>
#include <QObject>
#include <QPushButton>
#include <QTextEdit>
#include <QVector>

class QLineEdit;
class VideoWidget;

class MainWindowController : public QObject
{
  Q_OBJECT

public:
  struct UiRefs
  {
    VideoWidget *videoWidget = nullptr;
    QLineEdit *roiNameEdit = nullptr;
    QComboBox *roiPurposeCombo = nullptr;
    QComboBox *roiSelectorCombo = nullptr;
    QTextEdit *logView = nullptr;
    QPushButton *btnPlay = nullptr;
    QPushButton *btnApplyRoi = nullptr;
    QPushButton *btnFinishRoi = nullptr;
    QPushButton *btnDeleteRoi = nullptr;
  };

  explicit MainWindowController(const UiRefs &uiRefs, QObject *parent = nullptr);
  void shutdown();

private:
  void connectSignals();
  void initRoiDb();
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void flushSuppressedCameraLogs();
  void refreshRoiSelector();
  bool isValidRoiName(const QString &name, QString *errorMessage) const;
  bool isDuplicateRoiName(const QString &name) const;
  void playCctv();
  void onLogMessage(const QString &msg);
  void onOcrResult(int objectId, const QString &result);
  void onStartRoiDraw();
  void onFinishRoiDraw();
  void onDeleteRoi();
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onMetadataReceived(const QList<ObjectInfo> &objects);
  void onFrameCaptured(const QImage &frame);

  UiRefs m_ui;
  CameraManager *m_cameraManager = nullptr;
  PlateOcrCoordinator *m_ocrCoordinator = nullptr;
  MetadataSynchronizer m_metadataSynchronizer;
  RoiRepository m_roiRepository;
  int m_roiSequence = 0;
  QVector<QJsonObject> m_roiRecords;
  QString m_lastCameraLogMessage;
  qint64 m_lastCameraLogMs = 0;
  int m_suppressedCameraLogCount = 0;
};

#endif // MAINWINDOWCONTROLLER_H
