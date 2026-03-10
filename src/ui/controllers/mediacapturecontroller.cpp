#include "mediacapturecontroller.h"
#include "database/mediarepository.h"
#include "video/mediarecorderworker.h"
#include "video/videobuffermanager.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QThread>
#include <QTimer>

MediaCaptureController::MediaCaptureController(const UiRefs &ui,
                                               MediaRepository *mediaRepo,
                                               Context ctx, QObject *parent)
    : QObject(parent), m_ui(ui), m_ctx(std::move(ctx)), m_mediaRepo(mediaRepo) {
  // 4-channel capture/manual-record buffers (600 frames ≈ 40s at 15 fps)
  for (int i = 0; i < 4; ++i) {
    m_buffers[i] = new VideoBufferManager(600, this);
  }

  // Continuous-recording buffers (5 FPS × 60s = 300 frames, use 600 for
  // headroom)
  for (int i = 0; i < 4; ++i) {
    m_continuousBuffers[i] = new VideoBufferManager(600, this);
  }

  // Background recorder thread
  m_recorderWorker = new MediaRecorderWorker();
  m_recorderThread = new QThread(this);
  m_recorderWorker->moveToThread(m_recorderThread);
  connect(m_recorderThread, &QThread::finished, m_recorderWorker,
          &QObject::deleteLater);
  connect(m_recorderWorker, &MediaRecorderWorker::finished, this,
          &MediaCaptureController::onMediaSaveFinished);
  m_recorderThread->start();

  // Continuous-recording timer (1 min interval)
  m_continuousRecordTimer = new QTimer(this);
  m_continuousRecordTimer->setInterval(60000);
  connect(m_continuousRecordTimer, &QTimer::timeout, this,
          &MediaCaptureController::onContinuousRecordTimeout);

  // Cleanup timer (1 min interval)
  m_cleanupTimer = new QTimer(this);
  m_cleanupTimer->setInterval(60000);
  connect(m_cleanupTimer, &QTimer::timeout, this,
          &MediaCaptureController::onCleanupTimeout);

  m_continuousRecordTimer->start();
  m_cleanupTimer->start();
}

MediaCaptureController::~MediaCaptureController() { shutdown(); }

void MediaCaptureController::connectSignals() {
  if (m_ui.btnCaptureManual) {
    connect(m_ui.btnCaptureManual, &QPushButton::clicked, this,
            &MediaCaptureController::onCaptureManual);
  }
  if (m_ui.btnCaptureRecordTab) {
    connect(m_ui.btnCaptureRecordTab, &QPushButton::clicked, this,
            &MediaCaptureController::onCaptureManual);
  }
  if (m_ui.btnRecordManual) {
    connect(m_ui.btnRecordManual, &QPushButton::toggled, this,
            &MediaCaptureController::onRecordManualToggled);
  }
  if (m_ui.btnRecordRecordTab) {
    connect(m_ui.btnRecordRecordTab, &QPushButton::toggled, this,
            &MediaCaptureController::onRecordManualToggled);
  }
  if (m_ui.btnApplyContinuousSetting) {
    connect(m_ui.btnApplyContinuousSetting, &QPushButton::clicked, this,
            &MediaCaptureController::onApplyContinuousSettingClicked);
  }
}

void MediaCaptureController::shutdown() {
  if (m_continuousRecordTimer) {
    m_continuousRecordTimer->stop();
  }
  if (m_cleanupTimer) {
    m_cleanupTimer->stop();
  }

  if (m_recorderThread) {
    m_recorderThread->quit();
    m_recorderThread->wait();
    m_recorderThread = nullptr;
  }

  for (int i = 0; i < 4; ++i) {
    delete m_continuousBuffers[i];
    m_continuousBuffers[i] = nullptr;
  }
}

VideoBufferManager *MediaCaptureController::buffer(int index) const {
  if (index < 0 || index >= 4) {
    return nullptr;
  }
  return m_buffers[index];
}

void MediaCaptureController::setSelectedChannelIndex(int index) {
  m_selectedChannelIndex = index;
}

void MediaCaptureController::onRawFrameReady(int cardIndex,
                                             QSharedPointer<cv::Mat> framePtr,
                                             qint64 timestampMs) {
  if (cardIndex < 0 || cardIndex >= 4 || !framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > 100) {
    return;
  }

  // Continuous-recording buffer (throttle to ~5 FPS)
  if (m_continuousBuffers[cardIndex]) {
    if (!m_continuousThrottleTimers[cardIndex].isValid() ||
        m_continuousThrottleTimers[cardIndex].elapsed() >= 200) {
      m_continuousBuffers[cardIndex]->addFrame(framePtr);
      m_continuousThrottleTimers[cardIndex].restart();
    }
  }

  // Manual capture / event-segment buffer (full rate)
  if (m_buffers[cardIndex]) {
    m_buffers[cardIndex]->addFrame(framePtr);
  }
}

void MediaCaptureController::onCaptureManual() {
  QPushButton *senderBtn = qobject_cast<QPushButton *>(sender());
  int idx = m_selectedChannelIndex;

  if (!senderBtn && m_ui.cmbManualCamera) {
    idx = m_ui.cmbManualCamera->currentIndex();
  } else if (senderBtn && m_ui.btnCaptureRecordTab &&
             senderBtn == m_ui.btnCaptureRecordTab) {
    idx = m_ui.cmbManualCamera ? m_ui.cmbManualCamera->currentIndex() : 0;
  }
  if (idx < 0)
    idx = 0;

  VideoBufferManager *targetBuffer = buffer(idx);
  QString camId = QString("Ch %1").arg(idx + 1);

  emit logMessage(QString("[Recorder] [%1] 수동 캡처 요청...").arg(camId));

  if (!targetBuffer) {
    emit logMessage(
        QString("[Recorder] [%1] 버퍼 객체가 없습니다.").arg(camId));
    return;
  }

  auto frames = targetBuffer->getFrames();
  emit logMessage(QString("[Recorder] [%1] 버퍼 프레임 수: %2")
                      .arg(camId)
                      .arg(frames.size()));

  if (frames.empty()) {
    emit logMessage(
        QString("[Recorder] [%1] 버퍼가 비어있습니다. 해당 카메라가 "
                "실행 중인지 확인하세요.")
            .arg(camId));
    return;
  }

  QString fileName =
      QString("capture_Ch%1_%2.jpg")
          .arg(idx + 1)
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
  QString filePath = QDir(QCoreApplication::applicationDirPath())
                         .filePath("records/images/" + fileName);

  QMetaObject::invokeMethod(m_recorderWorker, "saveImage",
                            Q_ARG(QSharedPointer<cv::Mat>, frames.back()),
                            Q_ARG(QString, filePath), Q_ARG(QString, "IMAGE"),
                            Q_ARG(QString, "Manual Capture"),
                            Q_ARG(QString, camId));

  emit logMessage(
      QString("[Recorder] [%1] 캡처 저장 요청: %2").arg(camId, fileName));
}

void MediaCaptureController::onRecordManualToggled(bool checked) {
  // Sync toggle state between live-tab and record-tab buttons
  if (m_ui.btnRecordManual && m_ui.btnRecordManual->isChecked() != checked) {
    QSignalBlocker blocker(m_ui.btnRecordManual);
    m_ui.btnRecordManual->setChecked(checked);
  }
  if (m_ui.btnRecordRecordTab &&
      m_ui.btnRecordRecordTab->isChecked() != checked) {
    QSignalBlocker blocker(m_ui.btnRecordRecordTab);
    m_ui.btnRecordRecordTab->setChecked(checked);
  }

  auto updateButtonStyle = [&](QPushButton *btn, bool isRecording) {
    if (!btn)
      return;
    if (isRecording) {
      btn->setText("녹화 중지");
      btn->setStyleSheet(
          "background-color: #ff4d4d; color: white; "
          "font-weight: bold; border-radius: 4px; padding: 5px;");
    } else {
      btn->setText("수동 녹화");
      btn->setStyleSheet("");
    }
  };

  updateButtonStyle(m_ui.btnRecordManual, checked);
  updateButtonStyle(m_ui.btnRecordRecordTab, checked);

  if (checked) {
    m_isManualRecording = true;

    QPushButton *senderBtn = qobject_cast<QPushButton *>(sender());
    int idx = m_selectedChannelIndex;
    if (!senderBtn && m_ui.cmbManualCamera) {
      idx = m_ui.cmbManualCamera->currentIndex();
    } else if (senderBtn && m_ui.btnRecordRecordTab &&
               senderBtn == m_ui.btnRecordRecordTab) {
      idx = m_ui.cmbManualCamera ? m_ui.cmbManualCamera->currentIndex() : 0;
    }
    if (idx < 0)
      idx = 0;

    m_manualRecordChannelIdx = idx;
    VideoBufferManager *buf = buffer(idx);
    m_manualRecordStartIdx = buf ? buf->getTotalFramesAdded() : 0;

    QString camId = QString("Ch %1").arg(idx + 1);
    emit logMessage(QString("[Recorder] [%1] 수동 녹화 시작 (시작 인덱스: %2)")
                        .arg(camId)
                        .arg(m_manualRecordStartIdx));
  } else {
    int idx = m_manualRecordChannelIdx;
    VideoBufferManager *targetBuffer = buffer(idx);
    QString camId = QString("Ch %1").arg(idx + 1);

    emit logMessage(
        QString("[Recorder] [%1] 녹화 중지 요청 - 저장 중...").arg(camId));

    if (!targetBuffer) {
      emit logMessage(
          QString("[Recorder] [%1] 버퍼 객체가 없습니다.").arg(camId));
      m_isManualRecording = false;
      return;
    }
    m_isManualRecording = false;

    auto frames = targetBuffer->getFramesSince(m_manualRecordStartIdx);
    emit logMessage(QString("[Recorder] [%1] 버퍼 프레임 수: %2")
                        .arg(camId)
                        .arg(frames.size()));

    if (frames.empty()) {
      emit logMessage(
          QString("[Recorder] [%1] 버퍼가 비어있습니다. 해당 카메라가 "
                  "실행 중인지 확인하세요.")
              .arg(camId));
      return;
    }

    QString fileName =
        QString("record_Ch%1_%2.mp4")
            .arg(idx + 1)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = QDir(QCoreApplication::applicationDirPath())
                           .filePath("records/videos/" + fileName);

    QMetaObject::invokeMethod(
        m_recorderWorker, "saveVideo",
        Q_ARG(std::vector<QSharedPointer<cv::Mat>>, frames),
        Q_ARG(QString, filePath), Q_ARG(int, 15), Q_ARG(QString, "VIDEO"),
        Q_ARG(QString, "Manual Record"), Q_ARG(QString, camId));

    emit logMessage(QString("[Recorder] [%1] 녹화 파일 저장 실행: %2")
                        .arg(camId, fileName));
  }
}

void MediaCaptureController::onMediaSaveFinished(bool success,
                                                 const QString &filePath,
                                                 const QString &type,
                                                 const QString &desc,
                                                 const QString &cameraId) {
  if (!success) {
    emit logMessage(QString("[Recorder] 미디어 저장 실패: %1").arg(filePath));
    return;
  }

  QString fileName = QFileInfo(filePath).fileName();

  if (m_mediaRepo) {
    m_mediaRepo->addMediaRecord(type, desc, cameraId, filePath);
  }

  emit logMessage(QString("[Recorder] 미디어 저장 완료: %1").arg(fileName));
  emit mediaSaved();
}

void MediaCaptureController::onContinuousRecordTimeout() {
  int intervalMin = 1;

  for (int i = 0; i < 4; ++i) {
    if (!m_continuousBuffers[i])
      continue;

    auto frames = m_continuousBuffers[i]->getFrames(0, intervalMin * 60, 5);
    if (frames.empty())
      continue;

    QString camId = QString("Ch %1").arg(i + 1);
    QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString fileName =
        QString("continuous_Ch%1_%2.mp4").arg(i + 1).arg(timeStr);
    QString filePath = QDir(QCoreApplication::applicationDirPath())
                           .filePath("records/videos/" + fileName);

    QMetaObject::invokeMethod(
        m_recorderWorker, "saveVideo",
        Q_ARG(std::vector<QSharedPointer<cv::Mat>>, frames),
        Q_ARG(QString, filePath), Q_ARG(int, 5), Q_ARG(QString, "CONTINUOUS"),
        Q_ARG(QString, "상시녹화"), Q_ARG(QString, camId));
  }

  onCleanupTimeout();
}

void MediaCaptureController::onApplyContinuousSettingClicked() {
  emit logMessage(QString("[System] 상시녹화 설정 적용: 보존기간 %1분")
                      .arg(m_ui.spinRecordRetention->value()));
  onCleanupTimeout();
}

void MediaCaptureController::onCleanupTimeout() {
  int retentionMinutes =
      m_ui.spinRecordRetention ? m_ui.spinRecordRetention->value() : 60;
  if (!m_mediaRepo)
    return;

  QString error;
  auto oldRecords =
      m_mediaRepo->getOldMediaRecordsByMinutes(retentionMinutes, &error);

  if (!error.isEmpty()) {
    emit logMessage(QString("[Recorder] DB 조회 오류: %1").arg(error));
    return;
  }

  int deleteCount = 0;
  int failCount = 0;

  for (const auto &record : oldRecords) {
    if (record["type"].toString() != "CONTINUOUS")
      continue;

    int id = record["id"].toInt();
    QString path = record["file_path"].toString();

    if (QFile::remove(path)) {
      m_mediaRepo->deleteMediaRecord(id);
      deleteCount++;
    } else {
      if (!QFile::exists(path)) {
        m_mediaRepo->deleteMediaRecord(id);
        deleteCount++;
      } else {
        failCount++;
        qWarning() << "[Recorder] 파일 삭제 실패 (잠김 예상):" << path;
      }
    }
  }

  if (deleteCount > 0) {
    emit logMessage(
        QString("[Recorder] 상시녹화 오래된 파일 %1개 자동 정리 완료")
            .arg(deleteCount));
    emit mediaSaved();
  }

  if (failCount > 0) {
    emit logMessage(
        QString(
            "[Recorder] 상시녹화 파일 %1개를 삭제하지 못했습니다. (사용 중)")
            .arg(failCount));
  }
}

void MediaCaptureController::onEventRecordRequested(const QString &desc,
                                                    int preSec, int postSec) {
  int idx = m_ui.cmbManualCamera ? m_ui.cmbManualCamera->currentIndex() : 0;
  VideoBufferManager *targetBuffer = buffer(idx);

  if (!targetBuffer)
    return;

  uint64_t clickIdx = targetBuffer->getTotalFramesAdded();

  emit logMessage(
      QString("[Recorder] 이벤트 감지 (I:%1): %2초 후 저장을 시작합니다...")
          .arg(clickIdx)
          .arg(postSec));

  QString camId = QString("Ch %1").arg(idx + 1);

  QTimer::singleShot(
      postSec * 1000, this,
      [this, desc, preSec, postSec, idx, camId, targetBuffer, clickIdx]() {
        double actualFps = m_ctx.getLiveFps ? m_ctx.getLiveFps() : 15.0;
        if (actualFps <= 0)
          actualFps = 15.0;

        uint64_t startIdx =
            (clickIdx > static_cast<uint64_t>(preSec * actualFps))
                ? (clickIdx - static_cast<uint64_t>(preSec * actualFps))
                : 0;

        auto frames = targetBuffer->getFramesSince(startIdx);

        size_t requestedFrames =
            static_cast<size_t>((preSec + postSec) * actualFps);
        if (frames.size() > requestedFrames) {
          frames.resize(requestedFrames);
        }

        if (frames.empty()) {
          emit logMessage(
              QString::fromUtf8("[Recorder] 버퍼에 저장된 프레임이 없습니다."));
          return;
        }

        QString fileName =
            QString("event_Ch%1_%2.mp4")
                .arg(idx + 1)
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        QString filePath = QDir(QCoreApplication::applicationDirPath())
                               .filePath("records/videos/" + fileName);

        QMetaObject::invokeMethod(
            m_recorderWorker, "saveVideo",
            Q_ARG(std::vector<QSharedPointer<cv::Mat>>, frames),
            Q_ARG(QString, filePath), Q_ARG(int, static_cast<int>(actualFps)),
            Q_ARG(QString, "VIDEO"), Q_ARG(QString, desc),
            Q_ARG(QString, camId));

        emit logMessage(QString("[Recorder] 이벤트 구간 저장 완료: %1 "
                                "(%2초 전 ~ %3초 후, FPS: %4, 프레임수: %5)")
                            .arg(fileName)
                            .arg(preSec)
                            .arg(postSec)
                            .arg(actualFps)
                            .arg(frames.size()));
      });
}
