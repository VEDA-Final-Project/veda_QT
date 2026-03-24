#include "recordpanelcontroller.h"
#include <QCheckBox>
#include <QDebug>
#include <QFile>
#include <QHBoxLayout>
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

  if (m_ui.btnApplyEventSetting)
    connect(m_ui.btnApplyEventSetting, &QPushButton::clicked, this,
            &RecordPanelController::onApplyEventSettingClicked);

  if (m_ui.recordIntervalSpin) {
    m_ui.recordIntervalSpin->setRange(2, 40);
    m_ui.recordIntervalSpin->setValue(10);
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
              m_liveFpsTimer.invalidate();
              m_liveFramesSinceSample = 0;
              m_liveFpsEstimate = 15.0;
              // 채널 변경 시 기존 미디어 정지 → 라이브 모드
              onStopClicked();
              refreshLogTable();
            });
  }

  if (m_ui.btnViewContinuous) {
    connect(m_ui.btnViewContinuous, &QPushButton::clicked, this,
            &RecordPanelController::onViewContinuousClicked);
  }
}

// ──────────────────────────────────────────────────────────────
// 라이브 프리뷰 상태 관리
// ──────────────────────────────────────────────────────────────
double RecordPanelController::getLiveFps() const {
  return m_liveFpsEstimate > 0 ? m_liveFpsEstimate : 15.0;
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

  QVector<QJsonObject> fetchedRecords;
  if (m_ui.cmbManualCamera) {
    int idx = m_ui.cmbManualCamera->currentIndex();
    // MainWindowController가 "Ch 1", "Ch 2" 형식으로 저장하므로 형식을 맞춤
    QString camId = QString("Ch %1").arg(idx + 1);
    fetchedRecords = m_repo->getMediaRecordsByCamera(camId);
  } else {
    fetchedRecords = m_repo->getAllMediaRecords();
  }

  m_currentRecords.clear();
  for (int i = 0; i < fetchedRecords.size(); ++i) {
    if (fetchedRecords[i]["type"].toString() != "CONTINUOUS") {
      m_currentRecords.append(fetchedRecords[i]);
    }
  }

  for (int i = 0; i < m_currentRecords.size(); ++i) {
    const QJsonObject &row = m_currentRecords[i];

    int r = m_ui.recordLogTable->rowCount();
    m_ui.recordLogTable->insertRow(r);

    m_ui.recordLogTable->setItem(
        r, 0, new QTableWidgetItem(row["created_at"].toString()));
    m_ui.recordLogTable->setItem(r, 1,
                                 new QTableWidgetItem(row["type"].toString()));
    m_ui.recordLogTable->setItem(
        r, 2, new QTableWidgetItem(row["description"].toString()));

    QTableWidgetItem *dummyItem = new QTableWidgetItem();
    m_ui.recordLogTable->setItem(r, 3, dummyItem);

    QWidget *container = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(container);
    QCheckBox *checkBox = new QCheckBox();
    layout->addWidget(checkBox);
    layout->setAlignment(Qt::AlignCenter);
    layout->setContentsMargins(0, 0, 0, 0);
    m_ui.recordLogTable->setCellWidget(r, 3, container);
  }
  m_ui.recordLogTable->blockSignals(false);
}

// ──────────────────────────────────────────────────────────────
// 새로고침 / 삭제
// ──────────────────────────────────────────────────────────────

void RecordPanelController::onDeleteClicked() {
  if (!m_ui.recordLogTable)
    return;

  QVector<int> idsToDelete;
  QStringList filePathsToDelete;

  // 체크박스 선택된 항목 수집
  for (int i = 0; i < m_ui.recordLogTable->rowCount(); ++i) {
    QWidget *container = m_ui.recordLogTable->cellWidget(i, 3);
    if (!container)
      continue;
    QCheckBox *cb = container->findChild<QCheckBox *>();
    if (cb && cb->isChecked()) {
      if (i < m_currentRecords.size()) {
        idsToDelete.append(m_currentRecords[i]["id"].toInt());
        filePathsToDelete.append(m_currentRecords[i]["file_path"].toString());
      }
    }
  }

  // 만약 체크된 것이 없다면 현재 선택된 행 하나만 처리 (기존 동작 유지)
  if (idsToDelete.isEmpty()) {
    int row = m_ui.recordLogTable->currentRow();
    if (row >= 0 && row < m_currentRecords.size()) {
      idsToDelete.append(m_currentRecords[row]["id"].toInt());
      filePathsToDelete.append(m_currentRecords[row]["file_path"].toString());
    }
  }

  if (idsToDelete.isEmpty())
    return;

  QString message =
      idsToDelete.size() == 1
          ? QString::fromUtf8("정말로 이 기록을 삭제하시겠습니까?\n파일도 함께 "
                              "삭제됩니다.")
          : QString::fromUtf8("정말로 선택한 %1개의 기록을 "
                              "삭제하시겠습니까?\n파일도 함께 삭제됩니다.")
                .arg(idsToDelete.size());

  auto reply =
      QMessageBox::question(nullptr, QString::fromUtf8("삭제 확인"), message,
                            QMessageBox::Yes | QMessageBox::No);

  if (reply != QMessageBox::Yes)
    return;

  // 재생 중지
  onStopClicked();

  int successCount = 0;
  for (int i = 0; i < idsToDelete.size(); ++i) {
    if (m_repo->deleteMediaRecord(idsToDelete[i])) {
      if (QFile::exists(filePathsToDelete[i])) {
        QFile::remove(filePathsToDelete[i]);
      }
      successCount++;
    }
  }

  // 테이블 갱신
  refreshLogTable();

  if (m_ui.recordPreviewPathLabel)
    m_ui.recordPreviewPathLabel->setText(QString());

  qDebug() << "[RecordPanel] Deleted" << successCount << "records";
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
  m_isContinuousMode = false;
  m_continuousSegments.clear();
  m_currentChunkIdx = -1;

  if (m_ui.recordPreviewPathLabel) {
    auto formatSize = [](qint64 bytes) {
      if (bytes < 1024)
        return QString("%1 B").arg(bytes);
      if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
      return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    };

    m_ui.recordPreviewPathLabel->setText(
        QString::fromUtf8(
            "파일: %1 (%2)  |  ID: %3  |  카메라: %4\n시간: %5  |  경로: %6")
            .arg(fi.fileName())
            .arg(formatSize(fi.size()))
            .arg(m_currentRecords[row]["id"].toInt())
            .arg(m_currentRecords[row]["camera_id"].toString())
            .arg(m_currentRecords[row]["created_at"].toString())
            .arg(filePath));
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
  if (frame.isNull() || !m_ui.recordVideoWidget) {
    return;
  }

  if (!m_liveFpsTimer.isValid()) {
    m_liveFpsTimer.start();
    m_liveFramesSinceSample = 0;
  }
  ++m_liveFramesSinceSample;
  const qint64 elapsedMs = m_liveFpsTimer.elapsed();
  if (elapsedMs >= 1000) {
    m_liveFpsEstimate =
        (m_liveFramesSinceSample * 1000.0) / static_cast<double>(elapsedMs);
    m_liveFramesSinceSample = 0;
    m_liveFpsTimer.restart();
  }

  if (!m_hasMediaLoaded && !m_playCap.isOpened()) {
    m_ui.recordVideoWidget->updateFrame(frame);
  }
}

void RecordPanelController::updateLiveFrame(const SharedVideoFrame &frame) {
  if (!frame.isValid() || !frame.mat) {
    return;
  }

  const cv::Mat &mat = *frame.mat;
  if (mat.empty()) {
    return;
  }

  const QImage image(mat.data, mat.cols, mat.rows, mat.step,
                     QImage::Format_BGR888);
  updateLiveFrame(image.copy());
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
  m_isContinuousMode = false;
  m_continuousSegments.clear();
  m_currentChunkIdx = -1;

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

  if (m_isContinuousMode) {
    // 연속 모드: global frame 기준 해당 조각(chunk) 탐색
    for (int i = 0; i < m_continuousSegments.size(); ++i) {
      int startG = m_continuousSegments[i].startGlobalFrame;
      int endG = startG + m_continuousSegments[i].frameCount;
      if (targetFrame >= startG && targetFrame < endG) {
        if (m_currentChunkIdx != i) {
          openVideoChunk(i, targetFrame - startG);
        } else {
          m_playCap.set(cv::CAP_PROP_POS_FRAMES, targetFrame - startG);
        }
        break;
      }
    }
  } else {
    m_playCap.set(cv::CAP_PROP_POS_FRAMES, targetFrame);
  }

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
    if (m_isContinuousMode) {
      // 연속 재생 모드이면 다음 파일 조각 열기 시도
      if (m_currentChunkIdx + 1 < m_continuousSegments.size()) {
        if (openVideoChunk(m_currentChunkIdx + 1, 0)) {
          // 연달아서 바로 첫 프레임 읽어보기
          m_playCap >> frame;
        }
      }
    }
  }

  if (frame.empty()) {
    // 영상 완전 끝 → 정지 상태로 복귀
    onStopClicked();
    return;
  }

  cv::Mat rgb;
  cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
  QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
  m_ui.recordVideoWidget->updateFrame(qimg.copy());

  // 시크바 / 시간 표시 갱신
  if (m_totalFrames > 0 && m_fps > 0) {
    // 글로벌 프레임 위치 계산: 현재 청크 시작점 + 청크 내 현재 위치
    double localFrame = m_playCap.get(cv::CAP_PROP_POS_FRAMES);
    double globalFrame = localFrame;
    if (m_isContinuousMode && m_currentChunkIdx >= 0 &&
        m_currentChunkIdx < m_continuousSegments.size()) {
      globalFrame =
          m_continuousSegments[m_currentChunkIdx].startGlobalFrame + localFrame;
    }

    int sliderVal = static_cast<int>((globalFrame / m_totalFrames) * 1000.0);
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

  double totalSec = (m_fps > 0) ? m_totalFrames / m_fps : 0;

  if (m_isContinuousMode) {
    double globalCurSec = 0;
    if (m_currentChunkIdx >= 0 &&
        m_currentChunkIdx < m_continuousSegments.size()) {
      int localFrameIdx =
          static_cast<int>(m_playCap.get(cv::CAP_PROP_POS_FRAMES));
      int globalFrameIdx =
          m_continuousSegments[m_currentChunkIdx].startGlobalFrame +
          localFrameIdx;
      globalCurSec = globalFrameIdx / m_fps;
    }
    m_ui.videoTimeLabel->setText(formatTime(globalCurSec) + " / " +
                                 formatTime(totalSec));
  } else {
    double curSec = m_playCap.get(cv::CAP_PROP_POS_MSEC) / 1000.0;
    m_ui.videoTimeLabel->setText(formatTime(curSec) + " / " +
                                 formatTime(totalSec));
  }
}

// ──────────────────────────────────────────────────────────────
// 연속 재생(상시녹화) 커스텀 로직 추가
// ──────────────────────────────────────────────────────────────
void RecordPanelController::onViewContinuousClicked() {
  if (!m_ui.cmbManualCamera || !m_repo)
    return;

  int idx = m_ui.cmbManualCamera->currentIndex();
  QString camId = QString("Ch %1").arg(idx + 1);

  // 해당 카메라의 CONTINUOUS 타입 레코드 가져오기 (오래된 순 정렬)
  QVector<QJsonObject> records =
      m_repo->getMediaRecordsByTypeAndCamera("CONTINUOUS", camId);

  if (records.isEmpty()) {
    QMessageBox::information(nullptr, "알림",
                             "선택된 카메라의 상시 녹화 데이터가 없습니다.");
    return;
  }

  m_continuousSegments.clear();
  m_hasMediaLoaded = true;
  m_isContinuousMode = true;
  m_currentChunkIdx = -1;
  m_totalFrames = 0;
  m_fps = 5.0; // 상시녹화 기본 5프레임 (저장 설정과 동일하게)

  // 글로벌 프레임/시간 계산을 위해 각 파일의 길이 수집
  for (int i = 0; i < records.size(); ++i) {
    QString filePath = records[i]["file_path"].toString();
    QFileInfo fi(filePath);
    if (!fi.exists() || fi.size() == 0)
      continue;

    cv::VideoCapture tempCap(filePath.toLocal8Bit().toStdString());
    if (tempCap.isOpened()) {
      int fCount = static_cast<int>(tempCap.get(cv::CAP_PROP_FRAME_COUNT));
      double fps = tempCap.get(cv::CAP_PROP_FPS);
      if (fps > 0)
        m_fps = fps; // 마지막 유효 fps 채택

      if (fCount > 0) {
        VideoSegment seg;
        seg.filePath = filePath;
        seg.frameCount = fCount;
        seg.startGlobalFrame = m_totalFrames;
        m_continuousSegments.push_back(seg);
        m_totalFrames += fCount;
      }
      tempCap.release();
    }
  }

  if (m_continuousSegments.isEmpty() || m_totalFrames <= 0) {
    QMessageBox::warning(
        nullptr, "오름",
        "유효한 상시 녹화 동영상 파일을 열 수 없거나 내용이 비어있습니다.");
    m_hasMediaLoaded = false;
    m_isContinuousMode = false;
    return;
  }

  if (m_fps <= 0)
    m_fps = 5.0; // 최후의 보루 0 방지

  if (m_ui.recordPreviewPathLabel) {
    double totalSec = m_totalFrames / m_fps;
    m_ui.recordPreviewPathLabel->setText(
        QString::fromUtf8("상시 녹화 연속 모드: 총 ") +
        QString::number(m_continuousSegments.size()) +
        QString::fromUtf8("개 파일, 약 ") + formatTime(totalSec) +
        QString::fromUtf8(" 분량 길이"));
  }

  // 첫 번째 조각 열기 처리
  if (openVideoChunk(0)) {
    m_isPaused = true;
    updatePlayerControls(true);
    updateTimeLabel();
  } else {
    onStopClicked();
  }
}

bool RecordPanelController::openVideoChunk(int chunkIdx, int startFrame) {
  if (chunkIdx < 0 || chunkIdx >= m_continuousSegments.size())
    return false;

  if (m_playCap.isOpened())
    m_playCap.release();

  QString filePath = m_continuousSegments[chunkIdx].filePath;
  m_playCap.open(filePath.toLocal8Bit().toStdString());

  if (!m_playCap.isOpened()) {
    qDebug() << "[ContinuousPlay] Failed to open chunk:" << filePath;
    return false;
  }

  m_currentChunkIdx = chunkIdx;
  if (startFrame > 0 &&
      startFrame < m_continuousSegments[chunkIdx].frameCount) {
    m_playCap.set(cv::CAP_PROP_POS_FRAMES, startFrame);
  }

  // 정지 상태이고 첫 프레임 띄우기가 필요한 경우
  if (!m_playTimer->isActive()) {
    cv::Mat frame;
    m_playCap >> frame;
    if (!frame.empty() && m_ui.recordVideoWidget) {
      cv::Mat rgb;
      cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);
      QImage qimg(rgb.data, rgb.cols, rgb.rows, rgb.step,
                  QImage::Format_RGB888);
      m_ui.recordVideoWidget->updateFrame(qimg.copy());
      // 읽은 만큼 위치 되돌리기
      m_playCap.set(cv::CAP_PROP_POS_FRAMES, startFrame);
    }
  }
  return true;
}

// ──────────────────────────────────────────────────────────────
// 이벤트 구간 저장 트리거
// ──────────────────────────────────────────────────────────────
void RecordPanelController::onTriggerEventRecord() {
  QString desc = m_ui.recordEventTypeInput ? m_ui.recordEventTypeInput->text()
                                           : QString::fromUtf8("이벤트");
  int interval =
      m_ui.recordIntervalSpin ? m_ui.recordIntervalSpin->value() : 10;
  int preSec = interval / 2;
  int postSec = interval - preSec;

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
  }

  if (desc.trimmed().isEmpty())
    desc = QString::fromUtf8("수동 이벤트");

  if (m_ui.btnTriggerEventRecord) {
    m_ui.btnTriggerEventRecord->setText(QString::fromUtf8("⏺ 저장 중..."));
    m_ui.btnTriggerEventRecord->setStyleSheet(
        "background-color: #ff4d4d; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px;");

    QTimer::singleShot(postSec * 1000, this, [this]() {
      if (m_ui.btnTriggerEventRecord) {
        m_ui.btnTriggerEventRecord->setText(QString::fromUtf8("▶ 저장 실행"));
        m_ui.btnTriggerEventRecord->setStyleSheet(
            "background: #2563eb; color: white; border-radius: 4px; "
            "font-weight: bold;");
      }
    });
  }

  emit eventRecordRequested(desc, preSec, postSec);
}

void RecordPanelController::onApplyEventSettingClicked() {
  int interval =
      m_ui.recordIntervalSpin ? m_ui.recordIntervalSpin->value() : 10;
  qDebug() << "[RecordPanel] 이벤트 저장 구간 설정 적용:" << interval << "초";
}

void RecordPanelController::onRefreshClicked() {
  if (m_repo) {
    m_repo->cleanupMissingRecords();
  }
  refreshLogTable();
}
