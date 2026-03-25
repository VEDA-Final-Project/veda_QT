#include "recordingworkflowcontroller.h"

#include "application/db/parking/parkinglogapplicationservice.h"
#include "application/db/user/useradminapplicationservice.h"
#include "application/db/zone/zonequeryapplicationservice.h"
#include "infrastructure/camera/camerasource.h"
#include "infrastructure/persistence/mediarepository.h"
#include "infrastructure/video/mediarecorderworker.h"
#include "infrastructure/video/videobuffermanager.h"
#include "presentation/controllers/recordpanelcontroller.h"
#include "presentation/widgets/videowidget.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QThread>
#include <QTimer>

RecordingWorkflowController::RecordingWorkflowController(
    const UiRefs &uiRefs, Context context, MediaRepository *mediaRepo,
    RecordPanelController *recordPanel, QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)),
      m_mediaRepo(mediaRepo), m_recordPanelController(recordPanel) {
  for (VideoBufferManager *&buffer : m_captureBuffers) {
    buffer = new VideoBufferManager(600, this);
  }
  for (VideoBufferManager *&buffer : m_continuousBuffers) {
    buffer = new VideoBufferManager(600, this);
  }

  m_recorderWorker = new MediaRecorderWorker();
  m_recorderThread = new QThread(this);
  m_recorderWorker->moveToThread(m_recorderThread);
  connect(m_recorderThread, &QThread::finished, m_recorderWorker,
          &QObject::deleteLater);
  connect(m_recorderWorker, &MediaRecorderWorker::finished, this,
          &RecordingWorkflowController::onMediaSaveFinished);
  connect(m_recorderWorker, &MediaRecorderWorker::error, this,
          [this](const QString &message) {
            appendLog(QString("[Recorder] %1").arg(message));
          });
  m_recorderThread->start();

  m_continuousRecordTimer = new QTimer(this);
  m_continuousRecordTimer->setInterval(60000);
  connect(m_continuousRecordTimer, &QTimer::timeout, this,
          &RecordingWorkflowController::onContinuousRecordTimeout);

  m_cleanupTimer = new QTimer(this);
  m_cleanupTimer->setInterval(60000);
  connect(m_cleanupTimer, &QTimer::timeout, this,
          &RecordingWorkflowController::onCleanupTimeout);
}

void RecordingWorkflowController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnCaptureManual) {
    connect(m_ui.btnCaptureManual, &QPushButton::clicked, this,
            &RecordingWorkflowController::onCaptureManual);
  }
  if (m_ui.btnCaptureRecordTab) {
    connect(m_ui.btnCaptureRecordTab, &QPushButton::clicked, this,
            &RecordingWorkflowController::onCaptureManual);
  }
  if (m_ui.btnRecordManual) {
    connect(m_ui.btnRecordManual, &QPushButton::toggled, this,
            &RecordingWorkflowController::onRecordManualToggled);
  }
  if (m_ui.btnRecordRecordTab) {
    connect(m_ui.btnRecordRecordTab, &QPushButton::toggled, this,
            &RecordingWorkflowController::onRecordManualToggled);
  }
  if (m_ui.cmbManualCamera) {
    connect(m_ui.cmbManualCamera,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RecordingWorkflowController::bindRecordPreviewSource);
  }
  if (m_ui.btnApplyContinuousSetting) {
    connect(m_ui.btnApplyContinuousSetting, &QPushButton::clicked, this,
            &RecordingWorkflowController::onApplyContinuousSettingClicked);
  }
  if (m_recordPanelController) {
    connect(m_recordPanelController, &RecordPanelController::eventRecordRequested,
            this, &RecordingWorkflowController::onEventRecordRequested);
  }

  bindRecordPreviewSource(selectedManualCameraIndex());
  if (m_continuousRecordTimer) {
    m_continuousRecordTimer->start();
  }
  if (m_cleanupTimer) {
    m_cleanupTimer->start();
  }
}

void RecordingWorkflowController::shutdown() {
  bindRecordPreviewSource(-1);

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
}

void RecordingWorkflowController::triggerManualCapture() { onCaptureManual(); }

void RecordingWorkflowController::setManualRecordingFromHardware(bool recording) {
  if (m_isManualRecording == recording) {
    return;
  }

  if (m_ui.btnRecordManual) {
    m_ui.btnRecordManual->setChecked(recording);
    return;
  }

  onRecordManualToggled(recording);
}

bool RecordingWorkflowController::isManualRecording() const {
  return m_isManualRecording;
}

void RecordingWorkflowController::updateRecordPreviewSourceSize() {
  if (!m_recordPreviewSource || !m_ui.recordVideoWidget) {
    return;
  }

  m_recordPreviewSource->updateConsumerSize(kRecordPreviewConsumerId,
                                            m_ui.recordVideoWidget->size());
}

void RecordingWorkflowController::ingestRawFrame(int cardIndex,
                                                 SharedVideoFrame frame) {
  if (cardIndex < 0 || cardIndex >= kChannelCount || !frame.isValid()) {
    return;
  }

  QSharedPointer<cv::Mat> matForContinuous = frame.mat;
  QSharedPointer<cv::Mat> matForCapture = frame.mat;

  // Apply dynamic zoom crop if available.
  QRectF cropRect;
  if (cardIndex == selectedManualCameraIndex() && m_ui.recordVideoWidget) {
    QRectF recCrop = m_ui.recordVideoWidget->currentZoomRect();
    if (!recCrop.isNull()) {
        cropRect = recCrop;
    }
  }
  if (cropRect.isNull() && m_context.cameraZoomRect) {
    cropRect = m_context.cameraZoomRect(cardIndex);
  }

  if (!cropRect.isNull()) {
    int rx = static_cast<int>(cropRect.x() * frame.mat->cols);
    int ry = static_cast<int>(cropRect.y() * frame.mat->rows);
    int rw = static_cast<int>(cropRect.width() * frame.mat->cols);
    int rh = static_cast<int>(cropRect.height() * frame.mat->rows);
    rx = std::max(0, rx);
    ry = std::max(0, ry);
    rw = std::min(frame.mat->cols - rx, rw);
    rh = std::min(frame.mat->rows - ry, rh);

    if (rw > 0 && rh > 0 && (rw < frame.mat->cols || rh < frame.mat->rows)) {
      cv::Mat cropped = (*frame.mat)(cv::Rect(rx, ry, rw, rh));
      cv::Mat resized;
      cv::resize(cropped, resized, frame.mat->size(), 0, 0, cv::INTER_LINEAR);
      matForCapture = QSharedPointer<cv::Mat>::create(resized);
    }
  }

  if (m_continuousBuffers[cardIndex]) {
    if (!m_continuousThrottleTimers[cardIndex].isValid() ||
        m_continuousThrottleTimers[cardIndex].elapsed() >= 200) {
      m_continuousBuffers[cardIndex]->addFrame(matForContinuous);
      m_continuousThrottleTimers[cardIndex].restart();
    }
  }

  if (VideoBufferManager *targetBuffer = bufferByIndex(cardIndex)) {
    targetBuffer->addFrame(matForCapture);
  }
}

void RecordingWorkflowController::bindRecordPreviewSource(int index) {
  if (m_recordPreviewConnection) {
    disconnect(m_recordPreviewConnection);
    m_recordPreviewConnection = {};
  }

  if (m_recordPreviewSource) {
    m_recordPreviewSource->detachDisplayConsumer(kRecordPreviewConsumerId);
    m_recordPreviewSource = nullptr;
  }

  if (!m_recordPanelController) {
    return;
  }

  CameraSource *source = m_context.sourceAt ? m_context.sourceAt(index) : nullptr;
  if (!source) {
    return;
  }

  m_recordPreviewSource = source;
  m_recordPreviewConnection = connect(
      source, &CameraSource::displayFrameReady, this,
      [this](const QImage &frame, const QList<ObjectInfo> &) {
        if (m_recordPanelController) {
          m_recordPanelController->updateLiveFrame(frame);
        }
      });
  source->attachDisplayConsumer(kRecordPreviewConsumerId,
                                m_ui.recordVideoWidget
                                    ? m_ui.recordVideoWidget->size()
                                    : QSize());
  if (m_ui.recordVideoWidget) {
    m_ui.recordVideoWidget->setRecording(m_isManualRecording && m_manualRecordChannelIdx == index);
  }
}

void RecordingWorkflowController::onCaptureManual() {
  if (sender() == m_ui.btnCaptureManual) { // Only enforce on main UI
    if (m_context.selectedChannelCount && m_context.selectedChannelCount() > 1) {
      appendLog(QString("[Recorder] 다중 채널이 선택되어 캡처할 수 없습니다. 1개의 채널만 선택해주세요."));
      return;
    }
  }

  const int idx = resolveRequestedChannelIndex(sender());
  
  if (m_ui.cmbManualCamera && m_ui.cmbManualCamera->currentIndex() != idx) {
    bool oldState = m_ui.cmbManualCamera->blockSignals(true);
    m_ui.cmbManualCamera->setCurrentIndex(idx);
    m_ui.cmbManualCamera->blockSignals(oldState);
  }

  VideoBufferManager *targetBuffer = bufferByIndex(idx);
  const QString camId = QString("Ch %1").arg(idx + 1);

  appendLog(QString("[Recorder] [%1] 수동 캡처 요청...").arg(camId));

  if (!targetBuffer) {
    appendLog(QString("[Recorder] [%1] 버퍼 객체가 없습니다.").arg(camId));
    return;
  }

  auto frames = targetBuffer->getFrames();
  appendLog(QString("[Recorder] [%1] 버퍼 프레임 수: %2")
                .arg(camId)
                .arg(frames.size()));

  if (frames.empty()) {
    appendLog(QString("[Recorder] [%1] 버퍼가 비어있습니다. 해당 카메라가 실행 중인지 확인하세요.").arg(camId));
    return;
  }

  VideoWidget *flashTarget = nullptr;
  if (sender() == m_ui.btnCaptureRecordTab) {
    flashTarget = m_ui.recordVideoWidget;
  } else if (m_context.primarySelectedVideoWidget) {
    flashTarget = m_context.primarySelectedVideoWidget();
  }
  
  if (flashTarget) {
    flashTarget->showCaptureFlash();
  }

  const QString fileName =
      QString("capture_Ch%1_%2.jpg")
          .arg(idx + 1)
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
  const QString filePath = QDir(QCoreApplication::applicationDirPath())
                               .filePath("records/images/" + fileName);

  QMetaObject::invokeMethod(m_recorderWorker, "saveImage",
                            Q_ARG(QSharedPointer<cv::Mat>, frames.back()),
                            Q_ARG(QString, filePath), Q_ARG(QString, "IMAGE"),
                            Q_ARG(QString, "Manual Capture"),
                            Q_ARG(QString, camId));

  appendLog(QString("[Recorder] [%1] 캡처 저장 완료: %2").arg(camId, fileName));
}

void RecordingWorkflowController::onRecordManualToggled(bool checked) {
  if (sender() == m_ui.btnRecordManual && checked) {
    if (m_context.selectedChannelCount && m_context.selectedChannelCount() > 1) {
      appendLog(QString("[Recorder] 다중 채널이 선택되어 녹화할 수 없습니다. 1개의 채널만 선택해주세요."));
      QSignalBlocker b(sender());
      m_ui.btnRecordManual->setChecked(false);
      return;
    }
  }

  syncManualRecordButtons(checked);

  if (checked) {
    m_isManualRecording = true;
    const int idx = resolveRequestedChannelIndex(sender());
    
    if (m_ui.cmbManualCamera && m_ui.cmbManualCamera->currentIndex() != idx) {
      bool oldState = m_ui.cmbManualCamera->blockSignals(true);
      m_ui.cmbManualCamera->setCurrentIndex(idx);
      m_ui.cmbManualCamera->blockSignals(oldState);
    }
    
    m_manualRecordChannelIdx = idx;
    VideoBufferManager *buffer = bufferByIndex(idx);
    m_manualRecordStartIdx = buffer ? buffer->getTotalFramesAdded() : 0;

    if (m_context.videoWidgetAt) {
      if (VideoWidget *vw = m_context.videoWidgetAt(idx)) {
        vw->setRecording(true);
      }
    }
    if (m_ui.recordVideoWidget) {
      m_ui.recordVideoWidget->setRecording(true);
    }

    appendLog(QString("[Recorder] [Ch %1] 수동 녹화 시작 (시작 인덱스: %2)")
                  .arg(idx + 1)
                  .arg(m_manualRecordStartIdx));
    return;
  }

  const int idx = m_manualRecordChannelIdx;
  VideoBufferManager *targetBuffer = bufferByIndex(idx);
  const QString camId = QString("Ch %1").arg(idx + 1);

  if (m_context.videoWidgetAt) {
    if (VideoWidget *vw = m_context.videoWidgetAt(idx)) {
      vw->setRecording(false);
    }
  }
  if (m_ui.recordVideoWidget) {
    m_ui.recordVideoWidget->setRecording(false);
  }

  appendLog(QString("[Recorder] [%1] 녹화 중지 요청 - 저장 중...").arg(camId));

  if (!targetBuffer) {
    appendLog(QString("[Recorder] [%1] 버퍼 객체가 없습니다.").arg(camId));
    m_isManualRecording = false;
    return;
  }

  m_isManualRecording = false;
  auto frames = targetBuffer->getFramesSince(m_manualRecordStartIdx);
  appendLog(QString("[Recorder] [%1] 버퍼 프레임 수: %2").arg(camId).arg(frames.size()));

  if (frames.empty()) {
    appendLog(QString("[Recorder] [%1] 버퍼가 비어있습니다. 해당 카메라가 실행 중인지 확인하세요.").arg(camId));
    return;
  }

  const QString fileName =
      QString("record_Ch%1_%2.mp4")
          .arg(idx + 1)
          .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
  const QString filePath = QDir(QCoreApplication::applicationDirPath())
                               .filePath("records/videos/" + fileName);

  QMetaObject::invokeMethod(
      m_recorderWorker, "saveVideo",
      Q_ARG(std::vector<QSharedPointer<cv::Mat>>, frames),
      Q_ARG(QString, filePath), Q_ARG(int, 15), Q_ARG(QString, "VIDEO"),
      Q_ARG(QString, "Manual Record"), Q_ARG(QString, camId));

  appendLog(QString("[Recorder] [%1] 녹화 파일 저장 실행: %2").arg(camId, fileName));
}

void RecordingWorkflowController::onEventRecordRequested(const QString &description, int preSec, int postSec) {
  const int idx = selectedManualCameraIndex();
  VideoBufferManager *targetBuffer = bufferByIndex(idx);
  if (!targetBuffer) return;

  const uint64_t clickIdx = targetBuffer->getTotalFramesAdded();
  appendLog(QString("[Recorder] 이벤트 감지 (I:%1): %2초 후 저장을 시작합니다...").arg(clickIdx).arg(postSec));

  const QString camId = QString("Ch %1").arg(idx + 1);
  QTimer::singleShot(postSec * 1000, this, [this, description, preSec, postSec, idx, camId, targetBuffer, clickIdx]() {
    double actualFps = m_recordPanelController ? m_recordPanelController->getLiveFps() : 15.0;
    if (actualFps <= 0) actualFps = 15.0;

    const uint64_t startIdx = (clickIdx > static_cast<uint64_t>(preSec * actualFps))
                                ? (clickIdx - static_cast<uint64_t>(preSec * actualFps))
                                : 0;

    auto framesToSave = targetBuffer->getFramesSince(startIdx);
    const size_t requestedFrames = static_cast<size_t>((preSec + postSec) * actualFps);
    if (framesToSave.size() > requestedFrames) {
        framesToSave.resize(requestedFrames);
    }

    if (framesToSave.empty()) {
        appendLog(QString("[Recorder] 버퍼에 저장된 프레임이 없습니다."));
        return;
    }

    const QString fileName = QString("event_Ch%1_%2.mp4")
                              .arg(idx + 1)
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString filePath = QDir(QCoreApplication::applicationDirPath())
                                 .filePath("records/events/" + fileName);

    QMetaObject::invokeMethod(m_recorderWorker, "saveVideo",
                              Q_ARG(std::vector<QSharedPointer<cv::Mat>>, framesToSave),
                              Q_ARG(QString, filePath),
                              Q_ARG(int, static_cast<int>(actualFps)),
                              Q_ARG(QString, "EVENT"),
                              Q_ARG(QString, description),
                              Q_ARG(QString, camId));

    appendLog(QString("[Recorder] 이벤트 구간 저장 완료: %1 (%2초 전 ~ %3초 후, FPS: %4, 프레임수: %5)")
                  .arg(fileName).arg(preSec).arg(postSec).arg(actualFps).arg(framesToSave.size()));
  });
}

void RecordingWorkflowController::onMediaSaveFinished(
    bool success, const QString &filePath, const QString &type,
    const QString &description, const QString &cameraId) {
  if (!success) {
    appendLog(QString("[Recorder] 미디어 저장 실패: %1").arg(filePath));
    return;
  }

  const QString fileName = QFileInfo(filePath).fileName();
  if (m_mediaRepo) {
    m_mediaRepo->addMediaRecord(type, description, cameraId, filePath);
  }

  appendLog(QString("[Recorder] 미디어 저장 완료: %1").arg(fileName));
  if (m_recordPanelController) {
    m_recordPanelController->refreshLogTable();
  }
}

void RecordingWorkflowController::onContinuousRecordTimeout() {
  constexpr int intervalMin = 1;

  for (int i = 0; i < kChannelCount; ++i) {
    if (!m_continuousBuffers[i]) {
      continue;
    }

    auto frames = m_continuousBuffers[i]->getFrames(0, intervalMin * 60, 5);
    if (frames.empty()) {
      continue;
    }

    const QString camId = QString("Ch %1").arg(i + 1);
    const QString timeStr =
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString fileName =
        QString("continuous_Ch%1_%2.mp4").arg(i + 1).arg(timeStr);
    const QString filePath = QDir(QCoreApplication::applicationDirPath())
                                 .filePath("records/videos/" + fileName);

    QMetaObject::invokeMethod(
        m_recorderWorker, "saveVideo",
        Q_ARG(std::vector<QSharedPointer<cv::Mat>>, frames),
        Q_ARG(QString, filePath), Q_ARG(int, 5), Q_ARG(QString, "CONTINUOUS"),
        Q_ARG(QString, "상시녹화"), Q_ARG(QString, camId));
  }

  onCleanupTimeout();
}

void RecordingWorkflowController::onApplyContinuousSettingClicked() {
  const int retentionMinutes =
      m_ui.spinRecordRetention ? m_ui.spinRecordRetention->value() : 60;
  appendLog(
      QString("[System] 상시녹화 설정 적용: 보존기간 %1분").arg(retentionMinutes));
  onCleanupTimeout();
}

void RecordingWorkflowController::onCleanupTimeout() {
  if (!m_mediaRepo) {
    return;
  }

  const int retentionMinutes =
      m_ui.spinRecordRetention ? m_ui.spinRecordRetention->value() : 60;
  QString error;
  const auto oldRecords =
      m_mediaRepo->getOldMediaRecordsByMinutes(retentionMinutes, &error);

  if (!error.isEmpty()) {
    appendLog(QString("[Recorder] DB 조회 오류: %1").arg(error));
    return;
  }

  int deleteCount = 0;
  int failCount = 0;
  for (const auto &record : oldRecords) {
    if (record["type"].toString() != "CONTINUOUS") {
      continue;
    }

    const int id = record["id"].toInt();
    const QString path = record["file_path"].toString();

    if (QFile::remove(path)) {
      m_mediaRepo->deleteMediaRecord(id);
      ++deleteCount;
      continue;
    }

    if (!QFile::exists(path)) {
      m_mediaRepo->deleteMediaRecord(id);
      ++deleteCount;
      continue;
    }

    ++failCount;
    qWarning() << "[Recorder] 파일 삭제 실패 (잠김 예상):" << path;
  }

  if (deleteCount > 0) {
    appendLog(QString("[Recorder] 상시녹화 오래된 파일 %1개 자동 정리 완료")
                  .arg(deleteCount));
    if (m_recordPanelController) {
      m_recordPanelController->refreshLogTable();
    }
  }

  if (failCount > 0) {
    appendLog(QString("[Recorder] 상시녹화 파일 %1개를 삭제하지 못했습니다. (사용 중)").arg(failCount));
  }
}

void RecordingWorkflowController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}

int RecordingWorkflowController::selectedManualCameraIndex() const {
  const int index = m_ui.cmbManualCamera ? m_ui.cmbManualCamera->currentIndex() : 0;
  return (index >= 0 && index < kChannelCount) ? index : 0;
}

int RecordingWorkflowController::resolveRequestedChannelIndex(
    QObject *senderObject) const {
  int idx = m_context.selectedCctvChannelIndex
                ? m_context.selectedCctvChannelIndex()
                : 0;
  QPushButton *senderButton = qobject_cast<QPushButton *>(senderObject);
  
  // 만약 REC 탭의 버튼에서 발생한 캡처/녹화 이벤트라면, 
  // REC 탭 콤보박스의 값을 타겟 카메라 인덱스로 사용한다.
  if ((senderButton && senderButton == m_ui.btnCaptureRecordTab) ||
      (senderButton && senderButton == m_ui.btnRecordRecordTab)) {
    idx = selectedManualCameraIndex();
  }
  // 그 외(팝업이나 메인 CCTV 제어 버튼)의 경우에는 현재 활성화된 CCTV 채널(idx)을 유지한다.

  return (idx >= 0 && idx < kChannelCount) ? idx : 0;
}

VideoBufferManager *RecordingWorkflowController::bufferByIndex(int index) const {
  if (index < 0 || index >= kChannelCount) {
    return nullptr;
  }
  return m_captureBuffers[static_cast<size_t>(index)];
}

void RecordingWorkflowController::syncManualRecordButtons(bool checked) {
  if (m_ui.btnRecordManual && m_ui.btnRecordManual->isChecked() != checked) {
    QSignalBlocker blocker(m_ui.btnRecordManual);
    m_ui.btnRecordManual->setChecked(checked);
  }
  if (m_ui.btnRecordRecordTab && m_ui.btnRecordRecordTab->isChecked() != checked) {
    QSignalBlocker blocker(m_ui.btnRecordRecordTab);
    m_ui.btnRecordRecordTab->setChecked(checked);
  }
  
  // Dashboard's btnRecordManual relies on its QSS (:checked pseudo-class).
  // Only update the Record Tab button explicitly.
  updateManualRecordButtonStyle(m_ui.btnRecordRecordTab, checked);
}

void RecordingWorkflowController::updateManualRecordButtonStyle(
    QPushButton *button, bool isRecording) const {
  if (!button) {
    return;
  }
  if (isRecording) {
    button->setStyleSheet("background-color: #f44336; color: white; "
                          "font-weight: bold; border-radius: 4px; padding: 4px;");
    button->setText("REC");
  } else {
    button->setStyleSheet("");
    button->setText("녹화");
  }
}
