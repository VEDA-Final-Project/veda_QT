#include "ui/windows/recordpanelcontroller.h"
#include "config/config.h"
#include "video/videobuffermanager.h"
#include <QDebug>
#include <QFile>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QScopeGuard>

// ──────────────────────────────────────────────────────────────
// 헬퍼: 초(double)를 "mm:ss" 문자열로 변환
// ──────────────────────────────────────────────────────────────
static QString formatTime(double seconds) {
  if (seconds < 0)
    seconds = 0;
  int s = static_cast<int>(seconds);
  return QString("%1:%2")
      .arg(s / 60, 2, 10, QLatin1Char('0'))
      .arg(s % 60, 2, 10, QLatin1Char('0'));
}

// ──────────────────────────────────────────────────────────────
// 생성자 / 소멸자
// ──────────────────────────────────────────────────────────────
RecordPanelController::RecordPanelController(const UiRefs &uiRefs,
                                             MediaRepository *repo,
                                             QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_repo(repo) {
  m_playTimer = new QTimer(this);
  connect(m_playTimer, &QTimer::timeout, this,
          &RecordPanelController::onPlayTimerTimeout);
}

RecordPanelController::~RecordPanelController() {
  stopLivePreview();
  if (m_playCap.isOpened())
    m_playCap.release();
}

// ──────────────────────────────────────────────────────────────
// 시그널 연결
// ──────────────────────────────────────────────────────────────
void RecordPanelController::connectSignals() {
  if (m_ui.btnRefreshRecordLogs)
    connect(m_ui.btnRefreshRecordLogs, &QPushButton::clicked, this,
            &RecordPanelController::onRefreshClicked);

  if (m_ui.btnDeleteRecordLog)
    connect(m_ui.btnDeleteRecordLog, &QPushButton::clicked, this,
            &RecordPanelController::onDeleteClicked);

  if (m_ui.recordLogTable)
    connect(m_ui.recordLogTable, &QTableWidget::itemSelectionChanged, this,
            &RecordPanelController::onRowSelectionChanged);

  if (m_ui.btnTriggerEventRecord)
    connect(m_ui.btnTriggerEventRecord, &QPushButton::clicked, this,
            &RecordPanelController::onTriggerEventRecord);

  if (m_ui.recordPreSecSpin) {
    m_ui.recordPreSecSpin->setRange(1, 39);
    m_ui.recordPreSecSpin->setValue(5);
  }
  if (m_ui.recordPostSecSpin) {
    m_ui.recordPostSecSpin->setRange(1, 39);
    m_ui.recordPostSecSpin->setValue(5);
  }

  // ── 플레이어 컨트롤 연결 ──
  if (m_ui.btnVideoPlay)
    connect(m_ui.btnVideoPlay, &QPushButton::clicked, this,
            &RecordPanelController::onPlayClicked);

  if (m_ui.btnVideoPause)
    connect(m_ui.btnVideoPause, &QPushButton::clicked, this,
            &RecordPanelController::onPauseClicked);

  if (m_ui.btnVideoStop)
    connect(m_ui.btnVideoStop, &QPushButton::clicked, this,
            &RecordPanelController::onStopClicked);

  if (m_ui.videoSeekSlider)
    connect(m_ui.videoSeekSlider, &QSlider::sliderMoved, this,
            &RecordPanelController::onSeekSliderMoved);

  if (m_ui.cmbManualCamera) {
    connect(m_ui.cmbManualCamera,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
              m_currentChannelIdx = idx;
              // 채널 변경 시 기존 미디어 정지 → 라이브 모드
              onStopClicked();
              refreshLogTable();
              // 선택된 채널의 RTSP 라이브 프리뷰 시작
              if (idx >= 0 && idx < m_channelKeys.size()) {
                startLivePreview(
                    Config::instance().rtspUrl(m_channelKeys[idx]));
              } else {
                stopLivePreview();
              }
            });
  }
}

// ──────────────────────────────────────────────────────────────
// RTSP 라이브 미리보기 관리
// ──────────────────────────────────────────────────────────────
void RecordPanelController::setChannelKeys(const QStringList &keys) {
  m_channelKeys = keys;
  int idx = m_ui.cmbManualCamera ? m_ui.cmbManualCamera->currentIndex() : 0;
  m_currentChannelIdx = idx;
  if (idx >= 0 && idx < m_channelKeys.size()) {
    startLivePreview(Config::instance().rtspUrl(m_channelKeys[idx]));
  }
}

void RecordPanelController::setVideoBuffers(VideoBufferManager *primary,
                                            VideoBufferManager *secondary,
                                            VideoBufferManager *buf3,
                                            VideoBufferManager *buf4) {
  m_primaryBuffer = primary;
  m_secondaryBuffer = secondary;
  m_buffer3 = buf3;
  m_buffer4 = buf4;
}

double RecordPanelController::getLiveFps() const {
  if (m_liveThread) {
    return m_liveThread->getActualFps();
  }
  return 15.0; // 기본값
}

VideoBufferManager *RecordPanelController::currentBuffer() const {
  switch (m_currentChannelIdx) {
  case 0:
    return m_primaryBuffer;
  case 1:
    return m_secondaryBuffer;
  case 2:
    return m_buffer3;
  case 3:
    return m_buffer4;
  default:
    return nullptr;
  }
}

void RecordPanelController::startLivePreview(const QString &rtspUrl) {
  if (rtspUrl.isEmpty())
    return;
  stopLivePreview();

  m_liveThread = new VideoThread(this);
  m_liveThread->setUrl(rtspUrl);
  m_liveThread->setTargetFps(15);
  connect(m_liveThread, &VideoThread::frameCaptured, this,
          [this](QSharedPointer<cv::Mat> frame, qint64) {
            if (!frame || frame->empty())
              return;

            // 선택된 채널에 맞는 버퍼에 프레임 누적 (캡처/녹화용)
            VideoBufferManager *buf = currentBuffer();
            if (buf)
              buf->addFrame(frame);

            // 미리보기 표시 (미디어 파일 미로드 + 동영상 재생 중 아닐 때)
            if (!m_hasMediaLoaded && !m_playCap.isOpened()) {
              QImage qimg(frame->data, frame->cols, frame->rows, frame->step,
                          QImage::Format_RGB888);
              if (m_ui.recordVideoWidget)
                m_ui.recordVideoWidget->updateFrame(qimg.copy());
            }
          });
  m_liveThread->start();
  qDebug() << "[RecordPanel] Live preview started for channel"
           << m_currentChannelIdx;
}

void RecordPanelController::stopLivePreview() {
  if (m_liveThread) {
    m_liveThread->disconnect(); // 시그널 연결 해제
    m_liveThread->stop();       // 스레드 루프 중지 요청
    // 스레드가 종료되면 메모리 해제하도록 예약 (비동기, 안전)
    connect(m_liveThread, &QThread::finished, m_liveThread,
            &QObject::deleteLater);
    m_liveThread = nullptr;
  }
}

// ──────────────────────────────────────────────────────────────
// 미디어 로그 테이블 갱신
// ──────────────────────────────────────────────────────────────
void RecordPanelController::refreshLogTable() {
  if (!m_ui.recordLogTable || !m_repo)
    return;

  // 테이블 갱신 중 selectionChanged 시그널 차단
  m_ui.recordLogTable->blockSignals(true);
  m_ui.recordLogTable->setRowCount(0);

  if (m_ui.cmbManualCamera) {
    int idx = m_ui.cmbManualCamera->currentIndex();
    // MainWindowController가 "Ch 1", "Ch 2" 형식으로 저장하므로 형식을 맞춤
    QString camId = QString("Ch %1").arg(idx + 1);
    m_currentRecords = m_repo->getMediaRecordsByCamera(camId);
  } else {
    m_currentRecords = m_repo->getAllMediaRecords();
  }

  for (int i = 0; i < m_currentRecords.size(); ++i) {
    const QJsonObject &row = m_currentRecords[i];
    int r = m_ui.recordLogTable->rowCount();
    m_ui.recordLogTable->insertRow(r);

    m_ui.recordLogTable->setItem(
        r, 0, new QTableWidgetItem(QString::number(row["id"].toInt())));
    m_ui.recordLogTable->setItem(r, 1,
                                 new QTableWidgetItem(row["type"].toString()));
    m_ui.recordLogTable->setItem(
        r, 2, new QTableWidgetItem(row["description"].toString()));
    m_ui.recordLogTable->setItem(
        r, 3, new QTableWidgetItem(row["camera_id"].toString()));
    m_ui.recordLogTable->setItem(
        r, 4, new QTableWidgetItem(row["created_at"].toString()));
    m_ui.recordLogTable->setItem(
        r, 5, new QTableWidgetItem(row["file_path"].toString()));
  }
  m_ui.recordLogTable->blockSignals(false);
}

// ──────────────────────────────────────────────────────────────
// 새로고침 / 삭제
// ──────────────────────────────────────────────────────────────
void RecordPanelController::onRefreshClicked() { refreshLogTable(); }

void RecordPanelController::onDeleteClicked() {
  if (!m_ui.recordLogTable)
    return;

  int row = m_ui.recordLogTable->currentRow();
  if (row < 0 || row >= m_currentRecords.size())
    return;

  int id = m_currentRecords[row]["id"].toInt();
  QString filePath = m_currentRecords[row]["file_path"].toString();

  auto reply = QMessageBox::question(
      nullptr, QString::fromUtf8("삭제 확인"),
      QString::fromUtf8(
          "정말로 이 기록을 삭제하시겠습니까?\n파일도 함께 삭제됩니다."),
      QMessageBox::Yes | QMessageBox::No);

  if (reply != QMessageBox::Yes)
    return;

  if (!m_repo->deleteMediaRecord(id))
    return;

  // 1) 재생 완전 정지
  onStopClicked();

  // 2) 파일 삭제
  if (QFile::exists(filePath))
    QFile::remove(filePath);

  // 3) 테이블 갱신 (blockSignals는 refreshLogTable 내부에서 처리)
  refreshLogTable();

  // 4) 미리보기 경로 레이블 초기화
  if (m_ui.recordPreviewPathLabel)
    m_ui.recordPreviewPathLabel->setText(QString());
}

// ──────────────────────────────────────────────────────────────
// 항목 선택 변경 시 → 미디어 로드 (재생은 버튼으로)
// ──────────────────────────────────────────────────────────────
void RecordPanelController::onRowSelectionChanged() {
  onStopClicked(); // 이전 재생 완전 정지

  if (!m_ui.recordLogTable || !m_ui.recordVideoWidget) {
    m_hasMediaLoaded = false;
    return;
  }

  int row = m_ui.recordLogTable->currentRow();
  if (row < 0 || row >= m_currentRecords.size()) {
    m_hasMediaLoaded = false;
    if (m_ui.recordPreviewPathLabel)
      m_ui.recordPreviewPathLabel->setText(
          QString::fromUtf8("선택된 파일: 없음"));
    return;
  }

  QString type = m_currentRecords[row]["type"].toString();
  QString filePath = m_currentRecords[row]["file_path"].toString();

  // 파일 존재 + 크기 확인 (0바이트 파일 열기 방지)
  QFileInfo fi(filePath);
  if (!fi.exists() || fi.size() == 0) {
    qDebug() << "[RecordPanel] File missing or empty:" << filePath;
    if (m_ui.recordPreviewPathLabel)
      m_ui.recordPreviewPathLabel->setText(
          QString::fromUtf8("파일을 찾을 수 없거나 비어있습니다: ") +
          fi.fileName());
    m_hasMediaLoaded = false;
    return;
  }

  // ★ 라이브 프레임 차단: 파일을 여는 동안 라이브 피드가 위젯을 건드리지 못하게
  m_hasMediaLoaded = true;

  if (m_ui.recordPreviewPathLabel) {
    auto formatSize = [](qint64 bytes) {
      if (bytes < 1024)
        return QString("%1 B").arg(bytes);
      if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
      return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    };

    m_ui.recordPreviewPathLabel->setText(
        QString::fromUtf8("파일: ") + fi.fileName() + " (" +
        formatSize(fi.size()) + ")" + QString::fromUtf8("  |  경로: ") +
        filePath);
  }

  if (type == "IMAGE") {
    cv::Mat img = cv::imread(filePath.toLocal8Bit().toStdString());
    if (!img.empty()) {
      cv::Mat rgb;
      cv::cvtColor(img, rgb, cv::COLOR_BGR2RGB);
      QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step,
                  QImage::Format_RGB888);
      m_ui.recordVideoWidget->updateFrame(qimg.copy());
    } else {
      qDebug() << "[RecordPanel] Failed to read image:" << filePath;
      m_hasMediaLoaded = false;
    }
    updatePlayerControls(false); // 이미지: 플레이어 비활성
  } else if (type == "VIDEO") {
    m_playCap.open(filePath.toLocal8Bit().toStdString());
    if (m_playCap.isOpened()) {
      m_fps = m_playCap.get(cv::CAP_PROP_FPS);
      if (m_fps <= 0 || m_fps > 120)
        m_fps = 30.0;
      m_totalFrames = static_cast<int>(m_playCap.get(cv::CAP_PROP_FRAME_COUNT));

      if (m_totalFrames <= 0) {
        qDebug() << "[RecordPanel] Video has 0 frames:" << filePath;
        m_playCap.release();
        m_hasMediaLoaded = false;
        updatePlayerControls(false);
        return;
      }

      // 첫 프레임 미리보기
      cv::Mat frame;
      m_playCap >> frame;
      if (!frame.empty()) {
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
        QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step,
                    QImage::Format_RGB888);
        m_ui.recordVideoWidget->updateFrame(qimg.copy());
      }
      m_playCap.set(cv::CAP_PROP_POS_FRAMES, 0);
      m_isPaused = true;
      updatePlayerControls(true);
      updateTimeLabel();
    } else {
      qDebug() << "[RecordPanel] Failed to open video:" << filePath;
      m_hasMediaLoaded = false;
      updatePlayerControls(false);
    }
  } else {
    m_hasMediaLoaded = false;
  }
}

void RecordPanelController::updateLiveFrame(const QImage &frame) {
  // 독립 RTSP 스레드를 사용하므로, 대시보드(MainWindowController)에서 보내주는
  // 프레임은 무시합니다. 이로써 두 소스가 widget을 번갈아 그리는 현상(깜빡임)을
  // 방지합니다.
  Q_UNUSED(frame);
}

// ──────────────────────────────────────────────────────────────
// 플레이어 컨트롤 슬롯 구현
// ──────────────────────────────────────────────────────────────
void RecordPanelController::onPlayClicked() {
  if (!m_playCap.isOpened())
    return;

  m_isPaused = false;
  m_hasMediaLoaded = true; // 재생 중에는 라이브 피드 차단
  m_playTimer->start(static_cast<int>(1000.0 / m_fps));

  if (m_ui.btnVideoPlay)
    m_ui.btnVideoPlay->setEnabled(false);
  if (m_ui.btnVideoPause)
    m_ui.btnVideoPause->setEnabled(true);
  if (m_ui.btnVideoStop)
    m_ui.btnVideoStop->setEnabled(true);
}

void RecordPanelController::onPauseClicked() {
  m_isPaused = true;
  m_playTimer->stop();

  if (m_ui.btnVideoPlay)
    m_ui.btnVideoPlay->setEnabled(true);
  if (m_ui.btnVideoPause)
    m_ui.btnVideoPause->setEnabled(false);
}

void RecordPanelController::onStopClicked() {
  m_playTimer->stop();
  m_isPaused = false;
  m_hasMediaLoaded = false; // idle로 복귀 → 라이브 피드 재개

  if (m_playCap.isOpened())
    m_playCap.release();

  if (m_ui.videoSeekSlider)
    m_ui.videoSeekSlider->setValue(0);

  updateTimeLabel();
  updatePlayerControls(false);
}

void RecordPanelController::onSeekSliderMoved(int value) {
  if (!m_playCap.isOpened() || m_totalFrames <= 0)
    return;

  // 슬라이더 0~1000 → 프레임 번호로 변환
  double ratio = static_cast<double>(value) / 1000.0;
  int targetFrame = static_cast<int>(ratio * m_totalFrames);
  m_playCap.set(cv::CAP_PROP_POS_FRAMES, targetFrame);

  // 현재 프레임 즉시 표시
  cv::Mat frame;
  m_playCap >> frame;
  if (!frame.empty() && m_ui.recordVideoWidget) {
    cv::Mat rgb;
    cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
    QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    m_ui.recordVideoWidget->updateFrame(qimg.copy());
  }
  updateTimeLabel();
}

// ──────────────────────────────────────────────────────────────
// 타이머 콜백 (프레임 한 장 디코드 후 표시)
// ──────────────────────────────────────────────────────────────
void RecordPanelController::onPlayTimerTimeout() {
  if (!m_playCap.isOpened() || !m_ui.recordVideoWidget || m_isPaused) {
    m_playTimer->stop();
    return;
  }

  cv::Mat frame;
  m_playCap >> frame;

  if (frame.empty()) {
    // 영상 끝 → 정지 상태로 복귀
    onStopClicked();
    return;
  }

  cv::Mat rgb;
  cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
  QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
  m_ui.recordVideoWidget->updateFrame(qimg.copy());

  // 시크바 / 시간 표시 갱신
  if (m_totalFrames > 0) {
    int curFrame = static_cast<int>(m_playCap.get(cv::CAP_PROP_POS_FRAMES));
    int sliderVal =
        static_cast<int>(static_cast<double>(curFrame) / m_totalFrames * 1000);
    if (m_ui.videoSeekSlider && !m_ui.videoSeekSlider->isSliderDown())
      m_ui.videoSeekSlider->setValue(sliderVal);
  }
  updateTimeLabel();
}

// ──────────────────────────────────────────────────────────────
// 유틸 함수
// ──────────────────────────────────────────────────────────────
void RecordPanelController::updatePlayerControls(bool hasVideo) {
  if (m_ui.btnVideoPlay)
    m_ui.btnVideoPlay->setEnabled(hasVideo);
  if (m_ui.btnVideoPause)
    m_ui.btnVideoPause->setEnabled(false);
  if (m_ui.btnVideoStop)
    m_ui.btnVideoStop->setEnabled(hasVideo);
  if (m_ui.videoSeekSlider) {
    m_ui.videoSeekSlider->setEnabled(hasVideo);
    if (!hasVideo)
      m_ui.videoSeekSlider->setValue(0);
  }
  if (!hasVideo && m_ui.videoTimeLabel)
    m_ui.videoTimeLabel->setText(QString::fromUtf8("00:00 / 00:00"));
}

void RecordPanelController::updateTimeLabel() {
  if (!m_ui.videoTimeLabel)
    return;
  if (!m_playCap.isOpened()) {
    m_ui.videoTimeLabel->setText(QString::fromUtf8("00:00 / 00:00"));
    return;
  }

  double curSec = m_playCap.get(cv::CAP_PROP_POS_MSEC) / 1000.0;
  double totalSec = (m_fps > 0) ? m_totalFrames / m_fps : 0;
  m_ui.videoTimeLabel->setText(formatTime(curSec) + " / " +
                               formatTime(totalSec));
}

// ──────────────────────────────────────────────────────────────
// 이벤트 구간 저장 트리거
// ──────────────────────────────────────────────────────────────
void RecordPanelController::onTriggerEventRecord() {
  QString desc = m_ui.recordEventTypeInput ? m_ui.recordEventTypeInput->text()
                                           : QString::fromUtf8("이벤트");
  int preSec = m_ui.recordPreSecSpin ? m_ui.recordPreSecSpin->value() : 5;
  int postSec = m_ui.recordPostSecSpin ? m_ui.recordPostSecSpin->value() : 5;

  // 전체 버퍼 크기(600프레임, 15fps 기준 약 40초)를 초과하지 않도록 검증
  const int MAX_TOTAL_SEC = 40;
  if (preSec + postSec > MAX_TOTAL_SEC) {
    QMessageBox::warning(nullptr, "알림",
                         QString("전체 저장 구간(%1초)이 버퍼 용량(%2초)을 "
                                 "초과했습니다.\n저장 구간을 조정합니다.")
                             .arg(preSec + postSec)
                             .arg(MAX_TOTAL_SEC));

    // 초과 시 비율에 맞춰 조정하거나 postSec을 우선 조정 (단순하게 postSec
    // 조정)
    postSec = MAX_TOTAL_SEC - preSec;
    if (postSec < 1)
      postSec = 1; // 최소 1초

    if (m_ui.recordPostSecSpin)
      m_ui.recordPostSecSpin->setValue(postSec);
  }

  if (desc.trimmed().isEmpty())
    desc = QString::fromUtf8("수동 이벤트");

  QString msg =
      QString::fromUtf8("[이벤트 예약] \"%1\" | 전:%2초 / 후:%3초 (대기 중...)")
          .arg(desc)
          .arg(preSec)
          .arg(postSec);
  setStatusText(msg);
  emit eventRecordRequested(desc, preSec, postSec);
}

void RecordPanelController::setStatusText(const QString &text) {
  if (m_ui.recordStatusLabel)
    m_ui.recordStatusLabel->setText(text);
}
