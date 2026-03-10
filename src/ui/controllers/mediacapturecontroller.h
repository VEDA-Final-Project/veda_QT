#ifndef MEDIACAPTURECONTROLLER_H
#define MEDIACAPTURECONTROLLER_H

#include <QElapsedTimer>
#include <QObject>
#include <QSharedPointer>
#include <functional>
#include <opencv2/core.hpp>
#include <vector>

class MediaRecorderWorker;
class MediaRepository;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QThread;
class QTimer;
class VideoBufferManager;

class MediaCaptureController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QPushButton *btnCaptureManual = nullptr;
    QPushButton *btnRecordManual = nullptr;
    QPushButton *btnCaptureRecordTab = nullptr;
    QPushButton *btnRecordRecordTab = nullptr;
    QComboBox *cmbManualCamera = nullptr;
    QSpinBox *spinRecordRetention = nullptr;
    QPushButton *btnApplyContinuousSetting = nullptr;
  };

  struct Context {
    std::function<double()> getLiveFps;
  };

  explicit MediaCaptureController(const UiRefs &ui, MediaRepository *mediaRepo,
                                  Context ctx, QObject *parent = nullptr);
  ~MediaCaptureController() override;

  void connectSignals();
  void shutdown();

  VideoBufferManager *buffer(int index) const;
  void setSelectedChannelIndex(int index);

public slots:
  void onRawFrameReady(int cardIndex, QSharedPointer<cv::Mat> framePtr,
                       qint64 timestampMs);
  void onCaptureManual();
  void onRecordManualToggled(bool checked);
  void onMediaSaveFinished(bool success, const QString &filePath,
                           const QString &type, const QString &desc,
                           const QString &cameraId);
  void onContinuousRecordTimeout();
  void onCleanupTimeout();
  void onApplyContinuousSettingClicked();
  void onEventRecordRequested(const QString &desc, int preSec, int postSec);

signals:
  void logMessage(const QString &msg);
  void mediaSaved();

private:
  UiRefs m_ui;
  Context m_ctx;
  MediaRepository *m_mediaRepo = nullptr;
  MediaRecorderWorker *m_recorderWorker = nullptr;
  QThread *m_recorderThread = nullptr;

  VideoBufferManager *m_buffers[4] = {};
  VideoBufferManager *m_continuousBuffers[4] = {};
  QElapsedTimer m_continuousThrottleTimers[4];
  QTimer *m_continuousRecordTimer = nullptr;
  QTimer *m_cleanupTimer = nullptr;

  bool m_isManualRecording = false;
  int m_manualRecordChannelIdx = -1;
  uint64_t m_manualRecordStartIdx = 0;
  int m_selectedChannelIndex = 0;
};

#endif // MEDIACAPTURECONTROLLER_H
