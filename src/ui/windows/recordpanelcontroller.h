#ifndef RECORDPANELCONTROLLER_H
#define RECORDPANELCONTROLLER_H

#include "database/mediarepository.h"
#include "ui/video/videowidget.h"
#include "video/videothread.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QSharedPointer>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVector>
#include <opencv2/opencv.hpp>

class VideoBufferManager;

class RecordPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QTableWidget *recordLogTable = nullptr;
    QPushButton *btnRefreshRecordLogs = nullptr;
    QPushButton *btnDeleteRecordLog = nullptr;
    VideoWidget *recordVideoWidget = nullptr;
    QLineEdit *recordEventTypeInput = nullptr;
    QSpinBox *recordPreSecSpin = nullptr;
    QSpinBox *recordPostSecSpin = nullptr;
    QPushButton *btnTriggerEventRecord = nullptr;
    QComboBox *cmbManualCamera = nullptr;
    QLabel *recordStatusLabel = nullptr;
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
  void setChannelKeys(const QStringList &keys);
  void setVideoBuffers(VideoBufferManager *primary,
                       VideoBufferManager *secondary, VideoBufferManager *buf3,
                       VideoBufferManager *buf4);
  double getLiveFps() const;

public slots:
  void onRefreshClicked();
  void onDeleteClicked();
  void onRowSelectionChanged();
  void onTriggerEventRecord();
  void setStatusText(const QString &text);
  void updateLiveFrame(const QImage &frame);
  void onViewContinuousClicked();

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
  void startLivePreview(const QString &rtspUrl);
  void stopLivePreview();
  VideoBufferManager *currentBuffer() const;
  bool openVideoChunk(int chunkIdx, int startFrame = 0);
  void updateContinuousSeekSlider();

  bool m_hasMediaLoaded = false;
  bool m_isContinuousMode = false;
  QVector<VideoSegment> m_continuousSegments;
  int m_currentChunkIdx = -1;

  // 독립 RTSP 미리보기 스레드
  VideoThread *m_liveThread = nullptr;
  QStringList m_channelKeys;
  int m_currentChannelIdx = 0;

  // 캡처/녹화용 버퍼 (Ch1~Ch4)
  VideoBufferManager *m_primaryBuffer = nullptr;
  VideoBufferManager *m_secondaryBuffer = nullptr;
  VideoBufferManager *m_buffer3 = nullptr;
  VideoBufferManager *m_buffer4 = nullptr;
};

#endif // RECORDPANELCONTROLLER_H
