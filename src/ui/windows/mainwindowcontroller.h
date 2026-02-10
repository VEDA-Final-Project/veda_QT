#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include "core/camerasessionservice.h"
#include "core/logdeduplicator.h"
#include "core/roiservice.h"
#include "ocr/plateocrcoordinator.h"
#include <QComboBox>
#include <QJsonObject>
#include <QObject>
#include <QPushButton>
#include <QTextEdit>

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
  void refreshRoiSelector();
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
  CameraSessionService m_cameraSession;
  RoiService m_roiService;
  LogDeduplicator m_logDeduplicator;
};

#endif // MAINWINDOWCONTROLLER_H
