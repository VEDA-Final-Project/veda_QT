#ifndef RECORDINGWORKFLOWCONTROLLER_H
#define RECORDINGWORKFLOWCONTROLLER_H

#include "infrastructure/video/sharedvideoframe.h"
#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QRectF>
#include <QString>
#include <array>
#include <cstdint>
#include <functional>

class CameraSource;
class MediaRecorderWorker;
class MediaRepository;
class QPushButton;
class QComboBox;
class QThread;
class QTimer;
class QSpinBox;
class RecordPanelController;
class VideoBufferManager;
class VideoWidget;

class RecordingWorkflowController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QPushButton *btnCaptureManual = nullptr;
    QPushButton *btnCaptureRecordTab = nullptr;
    QPushButton *btnRecordManual = nullptr;
    QPushButton *btnRecordRecordTab = nullptr;
    QPushButton *btnApplyContinuousSetting = nullptr;
    QComboBox *cmbManualCamera = nullptr;
    QSpinBox *spinRecordRetention = nullptr;
    VideoWidget *recordVideoWidget = nullptr;
  };

  struct Context {
    std::function<void(const QString &)> logMessage;
    std::function<int()> selectedCctvChannelIndex;
    std::function<CameraSource *(int)> sourceAt;
    std::function<int()> selectedChannelCount;
    std::function<VideoWidget *()> primarySelectedVideoWidget;
    std::function<VideoWidget *(int)> videoWidgetAt;
    std::function<QRectF(int)> cameraZoomRect;
  };

  explicit RecordingWorkflowController(const UiRefs &uiRefs, Context context,
                                       MediaRepository *mediaRepo,
                                       RecordPanelController *recordPanel,
                                       QObject *parent = nullptr);

  void connectSignals();
  void shutdown();
  void triggerManualCapture();
  void setManualRecordingFromHardware(bool recording);
  bool isManualRecording() const;
  void updateRecordPreviewSourceSize();
  void ingestRawFrame(int cardIndex, SharedVideoFrame frame);

private slots:
  void bindRecordPreviewSource(int index);
  void onCaptureManual();
  void onRecordManualToggled(bool checked);
  void onEventRecordRequested(const QString &description, int preSec,
                              int postSec);
  void onMediaSaveFinished(bool success, const QString &filePath,
                           const QString &type, const QString &description,
                           const QString &cameraId);
  void onContinuousRecordTimeout();
  void onApplyContinuousSettingClicked();
  void onCleanupTimeout();

private:
  static constexpr int kRecordPreviewConsumerId = 100;
  static constexpr int kChannelCount = 4;

  void appendLog(const QString &message) const;
  int selectedManualCameraIndex() const;
  int resolveRequestedChannelIndex(QObject *senderObject) const;
  VideoBufferManager *bufferByIndex(int index) const;
  void syncManualRecordButtons(bool checked);
  void updateManualRecordButtonStyle(QPushButton *button,
                                     bool isRecording) const;

  UiRefs m_ui;
  Context m_context;
  MediaRepository *m_mediaRepo = nullptr;
  RecordPanelController *m_recordPanelController = nullptr;
  std::array<VideoBufferManager *, kChannelCount> m_captureBuffers{
      {nullptr, nullptr, nullptr, nullptr}};
  std::array<VideoBufferManager *, kChannelCount> m_continuousBuffers{
      {nullptr, nullptr, nullptr, nullptr}};
  std::array<QElapsedTimer, kChannelCount> m_continuousThrottleTimers;
  MediaRecorderWorker *m_recorderWorker = nullptr;
  QThread *m_recorderThread = nullptr;
  CameraSource *m_recordPreviewSource = nullptr;
  QMetaObject::Connection m_recordPreviewConnection;
  QTimer *m_continuousRecordTimer = nullptr;
  QTimer *m_cleanupTimer = nullptr;
  bool m_isManualRecording = false;
  int m_manualRecordChannelIdx = -1;
  uint64_t m_manualRecordStartIdx = 0;
  bool m_signalsConnected = false;
};

#endif // RECORDINGWORKFLOWCONTROLLER_H
