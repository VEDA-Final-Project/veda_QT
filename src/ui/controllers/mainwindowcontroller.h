#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QTimer>
#include <array>
#include <opencv2/core.hpp>

#include "logging/logdeduplicator.h"
#include "ui/windows/mainwindowuirefs.h"

class CameraChannelRuntime;
class CameraSource;
class DbPanelController;
class MediaCaptureController;
class MediaRepository;
class ParkingService;
class QEvent;
class QThread;
class RecordPanelController;
class RoiPanelController;
class RoiService;
class RpiPanelController;
class TelegramBotAPI;
class TelegramPanelController;
class VideoWidget;

class MainWindowController : public QObject {
  Q_OBJECT

public:
  explicit MainWindowController(const MainWindowUiRefs &uiRefs,
                                QObject *parent = nullptr);
  void shutdown();
  void startInitialCctv();

signals:
  void primaryVideoReady();

public slots:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void onVideoWidgetResizedSlot();

  void connectSignals();
  void initRoiDb();
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void onLogMessage(const QString &msg);
  void onChannelCardClicked(int index);
  void onSystemConfigChanged();
  void onReidTableCellClicked(int row, int column);

private:
  enum class RoiTarget { Primary = 0, Secondary = 1 };

  void initChannelCards();
  void initRoiDbForChannels();
  void reloadRoiForTarget(RoiTarget target, bool writeLog = true);
  void refreshZoneTableAllChannels();
  void updateChannelCardSelection();
  void startCameraSources();
  void updateThumbnailForCard(int cardIndex, const QImage &image);
  bool isCameraSourceRunning(int cardIndex) const;
  CameraChannelRuntime *channelAt(int index) const;
  CameraSource *sourceAt(int cardIndex) const;
  VideoWidget *videoWidgetForTarget(RoiTarget target) const;
  RoiService *roiServiceForTarget(RoiTarget target);
  const RoiService *roiServiceForTarget(RoiTarget target) const;
  ParkingService *parkingServiceForTarget(RoiTarget target);
  QString cameraKeyForTarget(RoiTarget target) const;

  MainWindowUiRefs m_ui;
  TelegramBotAPI *m_telegramApi = nullptr;

  // Sub-controllers
  RoiPanelController *m_roiPanelController = nullptr;
  TelegramPanelController *m_telegramPanelController = nullptr;
  MediaCaptureController *m_mediaCaptureController = nullptr;
  RpiPanelController *m_rpiPanelController = nullptr;
  DbPanelController *m_dbPanelController = nullptr;
  RecordPanelController *m_recordPanelController = nullptr;
  MediaRepository *m_mediaRepo = nullptr;

  std::array<CameraChannelRuntime *, 2> m_channels{{nullptr, nullptr}};
  std::array<CameraSource *, 4> m_cameraSources{
      {nullptr, nullptr, nullptr, nullptr}};
  int m_selectedChannelIndex = 0;
  int m_secondaryChannelIndex = 1;
  LogDeduplicator m_logDeduplicator;
  QElapsedTimer m_renderTimerThumbs[4];
  QTimer *m_resizeDebounceTimer = nullptr;
};

#endif // MAINWINDOWCONTROLLER_H
