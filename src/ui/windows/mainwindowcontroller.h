#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPixmap>
#include <QPolygon>
#include <QRect>
#include <QSet>
#include <QSharedPointer>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>
#include <array>
#include <opencv2/core.hpp>
#include <vector>

#include "logging/logdeduplicator.h"
#include "mainwindowuirefs.h"
#include "telegram/telegrambotapi.h"
#include "video/sharedvideoframe.h"

class CameraChannelRuntime;
class CameraSource;
class DbPanelController;
class RecordPanelController;
class ControllerDialog;
class MediaRepository;
class VideoBufferManager;
class MediaRecorderWorker;
class QThread;
class ParkingService;
class RoiService;
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
  void onStartRoiDraw();
  void onCompleteRoiDraw();
  void onDeleteSelectedRoi();
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onRoiTargetChanged(int index);
  void onChannelCardClicked(int index);
  void onSystemConfigChanged();
  void onReidTableCellClicked(int row, int column);
  void onCaptureManual();
  void onRecordManualToggled(bool checked);
  void onMediaSaveFinished(bool success, const QString &filePath,
                           const QString &type, const QString &description,
                           const QString &cameraId);
  void onContinuousRecordTimeout();
  void onCleanupTimeout();
  void onContinuousSettingChanged();
  void onRefreshReidTableAllChannels();
  void onApplyContinuousSettingClicked();
  void onRawFrameReady(int cardIndex, SharedVideoFrame frame);


  void onSendEntry();
  void onSendExit();
  void onTelegramLog(const QString &msg);
  void onUsersUpdated(int count);
  void onPaymentConfirmed(const QString &plate, int amount);
  void onAdminSummoned(const QString &chatId, const QString &name);

private:
  enum class LiveLayoutMode { Single, Dual, Quad };
  enum class RoiTarget { Ch1 = 0, Ch2 = 1, Ch3 = 2, Ch4 = 3 };

  void initChannelCards();
  void initRoiDbForChannels();
  void reloadRoiForTarget(RoiTarget target, bool writeLog = true);
  void refreshRoiSelectorForTarget();
  void refreshZoneTableAllChannels();
  void refreshReidTableAllChannels(bool force = false);
  void ensureChannelSelected(int index);
  void rebuildLiveLayout();
  void applyLiveGridLayout(LiveLayoutMode mode);
  void updateChannelCardSelection();
  void startCameraSources();
  void bindRecordPreviewSource(int index);
  void updateRecordPreviewSourceSize();
  void updateThumbnailForCard(int cardIndex, SharedVideoFrame frame);
  void processJoystickMovement();
  void onHardwareButtonPressed(int btnCode);
  void onHardwareJoystickMoved(const QString &dir, int state);
  void onHardwareEncoderRotated(int delta);
  void onHardwareEncoderClicked();
  bool isChannelSelected(int index) const;
  bool isCameraSourceRunning(int cardIndex) const;
  int primarySelectedChannelIndex() const;
  int cardIndexForVideoWidget(const VideoWidget *videoWidget) const;
  LiveLayoutMode liveLayoutMode() const;
  CameraChannelRuntime *channelAt(int index) const;
  CameraChannelRuntime *channelForCardIndex(int cardIndex) const;
  CameraSource *sourceAt(int cardIndex) const;
  VideoWidget *videoWidgetForTarget(RoiTarget target) const;
  RoiService *roiServiceForTarget(RoiTarget target);
  const RoiService *roiServiceForTarget(RoiTarget target) const;
  ParkingService *parkingServiceForTarget(RoiTarget target);
  QString cameraKeyForTarget(RoiTarget target) const;
  QString roiTargetLabel(RoiTarget target) const;

  MainWindowUiRefs m_ui;
  RoiTarget m_roiTarget = RoiTarget::Ch1;
  TelegramBotAPI *m_telegramApi = nullptr;
  DbPanelController *m_dbPanelController = nullptr;
  RecordPanelController *m_recordPanelController = nullptr;
  MediaRepository *m_mediaRepo = nullptr;
  VideoBufferManager *m_primaryBuffer = nullptr;
  VideoBufferManager *m_secondaryBuffer = nullptr;
  VideoBufferManager *m_buffer3 = nullptr;
  VideoBufferManager *m_buffer4 = nullptr;

  VideoBufferManager *getBufferByIndex(int index) const;

  MediaRecorderWorker *m_recorderWorker = nullptr;
  QThread *m_recorderThread = nullptr;
  bool m_isManualRecording = false;
  int m_manualRecordChannelIdx = -1;
  uint64_t m_manualRecordStartIdx = 0;
  QString m_currentManualRecordPath;
  QTimer *m_joystickTimer = nullptr;
  double m_joystickTargetX = 0.0;
  double m_joystickTargetY = 0.0;

  std::array<CameraChannelRuntime *, 4> m_channels{
      {nullptr, nullptr, nullptr, nullptr}};
  std::array<CameraSource *, 4> m_cameraSources{
      {nullptr, nullptr, nullptr, nullptr}};
  CameraSource *m_recordPreviewSource = nullptr;
  QMetaObject::Connection m_recordPreviewConnection;
  int m_selectedChannelIndex = 0;
  QVector<int> m_selectedChannelIndices;
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

  // Continuous Recording
  VideoBufferManager *m_continuousBuffers[4] = {nullptr, nullptr, nullptr,
                                                 nullptr};
  QElapsedTimer m_continuousThrottleTimers[4];
  QTimer *m_continuousRecordTimer = nullptr;
  QTimer *m_cleanupTimer = nullptr;
  QTimer *m_reidTimer = nullptr;
  static constexpr int kRecordPreviewConsumerId = 100;

};

#endif // MAINWINDOWCONTROLLER_H
