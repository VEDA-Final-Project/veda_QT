#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include "logging/logdeduplicator.h"
#include "presentation/shell/mainwindowuirefs.h"
#include "infrastructure/video/sharedvideoframe.h"

class CameraSessionController;
class CameraSource;
class CctvController;
class DbPanelController;
class HardwareController;
class ChannelRuntimeController;
class ReidController;
class RecordPanelController;
class RecordingWorkflowController;
class TelegramPanelController;
class ControllerDialog;
class MediaRepository;
class ParkingService;

class MainWindowController : public QObject {
  Q_OBJECT

public:
  explicit MainWindowController(const MainWindowUiRefs &uiRefs,
                                QObject *parent = nullptr);
  void shutdown();
  void startInitialCctv();
  void connectControllerDialog(ControllerDialog *dialog);
  void setManualRecordingFromHardware(bool recording);

signals:
  void primaryVideoReady();

public slots:
  void connectSignals();
  void initRoiDb();
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void onLogMessage(const QString &msg);
  void onSystemConfigChanged();
  void onRawFrameReady(int cardIndex, SharedVideoFrame frame);

private:
  void initRoiDbForChannels();
  void refreshZoneTableAllChannels();
  CameraSource *sourceAt(int cardIndex) const;
  ParkingService *parkingServiceForCardIndex(int cardIndex) const;

  MainWindowUiRefs m_ui;
  ChannelRuntimeController *m_channelRuntimeController = nullptr;
  CameraSessionController *m_cameraSessionController = nullptr;
  CctvController *m_cctvController = nullptr;
  TelegramPanelController *m_telegramController = nullptr;
  DbPanelController *m_dbPanelController = nullptr;
  HardwareController *m_hardwareController = nullptr;
  ReidController *m_reidController = nullptr;
  RecordPanelController *m_recordPanelController = nullptr;
  RecordingWorkflowController *m_recordingWorkflowController = nullptr;
  MediaRepository *m_mediaRepo = nullptr;
  LogDeduplicator m_logDeduplicator;
};

#endif // MAINWINDOWCONTROLLER_H
