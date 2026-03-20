#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>
#include <array>
#include <opencv2/core.hpp>

#include "logging/logdeduplicator.h"
#include "presentation/shell/mainwindowuirefs.h"
#include "video/sharedvideoframe.h"

class CameraChannelRuntime;
class CameraSource;
class CctvController;
class DbPanelController;
class HardwareController;
class RecordPanelController;
class RecordingWorkflowController;
class TelegramPanelController;
class ControllerDialog;
class MediaRepository;
class ParkingService;
class VideoWidget;
class QEvent;

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
  bool eventFilter(QObject *obj, QEvent *event) override;
  void onVideoWidgetResizedSlot();

  void connectSignals();
  void initRoiDb();
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void onLogMessage(const QString &msg);
  void onSystemConfigChanged();
  void onReidTableCellClicked(int row, int column);
  void onRefreshReidTableAllChannels();
  void onRawFrameReady(int cardIndex, SharedVideoFrame frame);

private:
  void initRoiDbForChannels();
  void refreshZoneTableAllChannels();
  void refreshReidTableAllChannels(bool force = false);
  void startCameraSources();
  void updateThumbnailForCard(int cardIndex, SharedVideoFrame frame);
  int cardIndexForVideoWidget(const VideoWidget *videoWidget) const;
  CameraChannelRuntime *channelAt(int index) const;
  CameraChannelRuntime *channelForCardIndex(int cardIndex) const;
  CameraSource *sourceAt(int cardIndex) const;
  ParkingService *parkingServiceForCardIndex(int cardIndex);

  MainWindowUiRefs m_ui;
  CctvController *m_cctvController = nullptr;
  TelegramPanelController *m_telegramController = nullptr;
  DbPanelController *m_dbPanelController = nullptr;
  HardwareController *m_hardwareController = nullptr;
  RecordPanelController *m_recordPanelController = nullptr;
  RecordingWorkflowController *m_recordingWorkflowController = nullptr;
  MediaRepository *m_mediaRepo = nullptr;

  std::array<CameraChannelRuntime *, 4> m_channels{
      {nullptr, nullptr, nullptr, nullptr}};
  std::array<CameraSource *, 4> m_cameraSources{
      {nullptr, nullptr, nullptr, nullptr}};
  LogDeduplicator m_logDeduplicator;
  QElapsedTimer m_renderTimerThumbs[4];
  struct ThumbnailCache {
    const cv::Mat *frameIdentity = nullptr;
    QSize labelSize;
    QPixmap pixmap;
  };
  ThumbnailCache m_thumbnailCaches[4];
  QElapsedTimer m_reidRefreshTimer;
  QTimer *m_resizeDebounceTimer = nullptr;

  QTimer *m_reidTimer = nullptr;
};

#endif // MAINWINDOWCONTROLLER_H
