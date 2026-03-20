#ifndef RECORDPANELCONTROLLER_H
#define RECORDPANELCONTROLLER_H

#include "infrastructure/persistence/mediarepository.h"
#include "presentation/widgets/videowidget.h"
#include "infrastructure/video/sharedvideoframe.h"
#include <QElapsedTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVector>
#include <opencv2/opencv.hpp>

class RecordPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QTableWidget *recordLogTable = nullptr;
    QPushButton *btnRefreshRecordLogs = nullptr;
    QPushButton *btnDeleteRecordLog = nullptr;
    VideoWidget *recordVideoWidget = nullptr;
    QLineEdit *recordEventTypeInput = nullptr;
    QSpinBox *recordIntervalSpin = nullptr;
    QPushButton *btnApplyEventSetting = nullptr;
    QPushButton *btnTriggerEventRecord = nullptr;
    QComboBox *cmbManualCamera = nullptr;
    QLabel *recordPreviewPathLabel = nullptr;
    // Video player controls
    QPushButton *btnVideoPlay = nullptr;
    QPushButton *btnVideoPause = nullptr;
    QPushButton *btnVideoStop = nullptr;
    QSlider *videoSeekSlider = nullptr;
    QLabel *videoTimeLabel = nullptr;

    // Continuous Recording (상시 녹화)
    QSpinBox *spinRecordRetention = nullptr;
    QLabel *lblContinuousStatus = nullptr;
    QPushButton *btnViewContinuous = nullptr;
  };

  struct VideoSegment {
    QString filePath;
    int frameCount;
    // 글로벌 프레임 기준 시작 인덱스
    int startGlobalFrame;
  };

  explicit RecordPanelController(const UiRefs &uiRefs, MediaRepository *repo,
                                 QObject *parent = nullptr);
  ~RecordPanelController() override;

  void connectSignals();
  void refreshLogTable();
  double getLiveFps() const;

public slots:
  void onRefreshClicked();
  void onDeleteClicked();
  void onRowSelectionChanged();
  void onTriggerEventRecord();
  void updateLiveFrame(const SharedVideoFrame &frame);
  void onViewContinuousClicked();
  void onApplyEventSettingClicked();

private slots:
  void onPlayTimerTimeout();
  void onPlayClicked();
  void onPauseClicked();
  void onStopClicked();
  void onSeekSliderMoved(int value);

signals:
  void eventRecordRequested(const QString &description, int preSec,
                            int postSec);

private:
  UiRefs m_ui;
  MediaRepository *m_repo = nullptr;
  QVector<QJsonObject> m_currentRecords;

  QTimer *m_playTimer = nullptr;
  cv::VideoCapture m_playCap;
  bool m_isPaused = false;
  int m_totalFrames = 0;
  double m_fps = 30.0;

  void updatePlayerControls(bool hasVideo);
  void updateTimeLabel();
  bool openVideoChunk(int chunkIdx, int startFrame = 0);
  void updateContinuousSeekSlider();

  bool m_hasMediaLoaded = false;
  bool m_isContinuousMode = false;
  QVector<VideoSegment> m_continuousSegments;
  int m_currentChunkIdx = -1;

  int m_currentChannelIdx = 0;
  QElapsedTimer m_liveFpsTimer;
  int m_liveFramesSinceSample = 0;
  double m_liveFpsEstimate = 15.0;
};

#endif // RECORDPANELCONTROLLER_H
