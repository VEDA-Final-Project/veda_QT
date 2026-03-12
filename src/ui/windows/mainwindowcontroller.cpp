#include "mainwindowcontroller.h"

#include "camera/camerasource.h"
#include "camerachannelruntime.h"
#include "config/config.h"
#include "database/databasecontext.h"
#include "database/mediarepository.h"
#include "dbpanelcontroller.h"
#include "parking/parkingservice.h"
#include "recordpanelcontroller.h"
#include "roi/roiservice.h"
#include "rpipanelcontroller.h"
#include "ui/video/videowidget.h"
#include "video/mediarecorderworker.h"
#include "video/videobuffermanager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget> // User Table
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <algorithm>

namespace {
constexpr int kCameraStartStaggerMs = 350;
}

MainWindowController::MainWindowController(const MainWindowUiRefs &uiRefs,
                                           QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
  m_telegramApi = new TelegramBotAPI(this);

  CameraChannelRuntime::SharedUiRefs channelUiRefs;
  channelUiRefs.reidTable = m_ui.reidTable;
  channelUiRefs.staleTimeoutInput = m_ui.staleTimeoutInput;
  channelUiRefs.pruneTimeoutInput = m_ui.pruneTimeoutInput;
  channelUiRefs.chkShowStaleObjects = m_ui.chkShowStaleObjects;
  channelUiRefs.avgFpsLabel = m_ui.lblAvgFps;

  m_channels[0] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch1, QStringLiteral("Ch1"),
      m_ui.videoWidgets[0], channelUiRefs, this);
  m_channels[1] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch2, QStringLiteral("Ch2"),
      m_ui.videoWidgets[1], channelUiRefs, this);
  m_channels[2] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch3, QStringLiteral("Ch3"),
      m_ui.videoWidgets[2], channelUiRefs, this);
  m_channels[3] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch4, QStringLiteral("Ch4"),
      m_ui.videoWidgets[3], channelUiRefs, this);

  for (size_t i = 0; i < m_channels.size(); ++i) {
    if (!m_channels[i]) {
      continue;
    }
    connect(m_channels[i], &CameraChannelRuntime::zoneStateChanged, this,
            &MainWindowController::refreshZoneTableAllChannels);
  }
  if (m_channels[0]) {
    connect(m_channels[0], &CameraChannelRuntime::videoReady, this,
            &MainWindowController::primaryVideoReady);
  }

  RpiPanelController::UiRefs rpiUiRefs;
  rpiUiRefs.hostEdit = m_ui.rpiHostEdit;
  rpiUiRefs.portSpin = m_ui.rpiPortSpin;
  rpiUiRefs.btnConnect = m_ui.btnRpiConnect;
  rpiUiRefs.btnDisconnect = m_ui.btnRpiDisconnect;
  rpiUiRefs.btnBarrierUp = m_ui.btnBarrierUp;
  rpiUiRefs.btnBarrierDown = m_ui.btnBarrierDown;
  rpiUiRefs.btnLedOn = m_ui.btnLedOn;
  rpiUiRefs.btnLedOff = m_ui.btnLedOff;
  rpiUiRefs.connectionStatusLabel = m_ui.rpiConnectionStatusLabel;
  rpiUiRefs.vehicleStatusLabel = m_ui.rpiVehicleStatusLabel;
  rpiUiRefs.ledStatusLabel = m_ui.rpiLedStatusLabel;
  rpiUiRefs.irRawLabel = m_ui.rpiIrRawLabel;
  rpiUiRefs.servoAngleLabel = m_ui.rpiServoAngleLabel;
  rpiUiRefs.logView = m_ui.logView;
  m_rpiPanelController = new RpiPanelController(rpiUiRefs, this);

  const QString dbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/veda.db");
  DatabaseContext::init(dbPath);

  initChannelCards();
  startCameraSources();

  DbPanelController::UiRefs dbUiRefs;
  dbUiRefs.parkingLogTable = m_ui.parkingLogTable;
  dbUiRefs.plateSearchInput = m_ui.plateSearchInput;
  dbUiRefs.btnSearchPlate = m_ui.btnSearchPlate;
  dbUiRefs.btnRefreshLogs = m_ui.btnRefreshLogs;
  dbUiRefs.forcePlateInput = m_ui.forcePlateInput;
  dbUiRefs.forceObjectIdInput = m_ui.forceObjectIdInput;
  dbUiRefs.forceTypeInput = m_ui.forceTypeInput;
  dbUiRefs.forceScoreInput = m_ui.forceScoreInput;
  dbUiRefs.forceBBoxInput = m_ui.forceBBoxInput;
  dbUiRefs.btnForcePlate = m_ui.btnForcePlate;
  dbUiRefs.editPlateInput = m_ui.editPlateInput;
  dbUiRefs.btnEditPlate = m_ui.btnEditPlate;
  dbUiRefs.userDbTable = m_ui.userDbTable;
  dbUiRefs.btnRefreshUsers = m_ui.btnRefreshUsers;
  dbUiRefs.btnAddUser = m_ui.btnAddUser;
  dbUiRefs.btnEditUser = m_ui.btnEditUser;
  dbUiRefs.btnDeleteUser = m_ui.btnDeleteUser;
  dbUiRefs.hwLogTable = m_ui.hwLogTable;
  dbUiRefs.btnRefreshHwLogs = m_ui.btnRefreshHwLogs;
  dbUiRefs.btnClearHwLogs = m_ui.btnClearHwLogs;
  dbUiRefs.vehicleTable = m_ui.vehicleTable;
  dbUiRefs.btnRefreshVehicles = m_ui.btnRefreshVehicles;
  dbUiRefs.btnDeleteVehicle = m_ui.btnDeleteVehicle;
  dbUiRefs.zoneTable = m_ui.zoneTable;
  dbUiRefs.btnRefreshZone = m_ui.btnRefreshZone;
  dbUiRefs.logView = m_ui.logView;

  DbPanelController::Context dbContext;
  dbContext.parkingServiceProvider = [this]() {
    return parkingServiceForTarget(m_roiTarget);
  };
  dbContext.primaryZoneRecordsProvider = [this]() {
    CameraChannelRuntime *channel = channelAt(0);
    return channel ? channel->roiRecords() : QVector<QJsonObject>();
  };
  dbContext.secondaryZoneRecordsProvider = [this]() {
    CameraChannelRuntime *channel = channelAt(1);
    return channel ? channel->roiRecords() : QVector<QJsonObject>();
  };
  dbContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
  };
  dbContext.userDeleted = [this](const QString &chatId) {
    if (m_telegramApi) {
      m_telegramApi->removeUser(chatId);
    }
  };
  m_dbPanelController = new DbPanelController(dbUiRefs, dbContext, this);
  // 4. 녹화 조회 컨트롤러 초기화
  m_mediaRepo = new MediaRepository();
  m_mediaRepo->init();

  RecordPanelController::UiRefs recordUiRefs;
  recordUiRefs.recordLogTable = m_ui.recordLogTable;
  recordUiRefs.btnRefreshRecordLogs = m_ui.btnRefreshRecordLogs;
  recordUiRefs.btnDeleteRecordLog = m_ui.btnDeleteRecordLog;
  recordUiRefs.recordVideoWidget = m_ui.recordVideoWidget;
  recordUiRefs.recordEventTypeInput = m_ui.recordEventTypeInput;
  recordUiRefs.recordIntervalSpin = m_ui.recordIntervalSpin;
  recordUiRefs.btnApplyEventSetting = m_ui.btnApplyEventSetting;
  recordUiRefs.btnTriggerEventRecord = m_ui.btnTriggerEventRecord;
  recordUiRefs.recordPreviewPathLabel = m_ui.recordPreviewPathLabel;
  // 플레이어 컨트롤 연결
  recordUiRefs.btnVideoPlay = m_ui.btnVideoPlay;
  recordUiRefs.btnVideoPause = m_ui.btnVideoPause;
  recordUiRefs.btnVideoStop = m_ui.btnVideoStop;
  recordUiRefs.videoSeekSlider = m_ui.videoSeekSlider;
  recordUiRefs.videoTimeLabel = m_ui.videoTimeLabel;
  recordUiRefs.cmbManualCamera = m_ui.cmbManualCamera;

  // 상시 녹화 컨트롤 연결
  recordUiRefs.spinRecordRetention = m_ui.spinRecordRetention;
  recordUiRefs.lblContinuousStatus = m_ui.lblContinuousStatus;
  recordUiRefs.btnViewContinuous = m_ui.btnViewContinuous;

  m_recordPanelController =
      new RecordPanelController(recordUiRefs, m_mediaRepo, this);
  m_recordPanelController->connectSignals();
  m_recordPanelController->refreshLogTable();
  // 5. 녹화 시스템 초기화
  // 버퍼 크기를 600으로 확장 (15fps 기준 약 40초 분량 저장 가능)
  m_primaryBuffer = new VideoBufferManager(600, this);
  m_secondaryBuffer = new VideoBufferManager(600, this);
  m_buffer3 = new VideoBufferManager(600, this);
  m_buffer4 = new VideoBufferManager(600, this);

  // 이벤트 구간 저장: RecordPanel -> MainWindowController 연결
  // 클릭 시점 기준으로 '과거(preSec) + 미래(postSec)' 프레임을 모두 포함하기
  // 위해 postSec만큼 기다린 후 버퍼에서 프레임을 추출하여 저장합니다.
  connect(
      m_recordPanelController, &RecordPanelController::eventRecordRequested,
      this, [this](const QString &desc, int preSec, int postSec) {
        int idx =
            m_ui.cmbManualCamera ? m_ui.cmbManualCamera->currentIndex() : 0;
        VideoBufferManager *targetBuffer = getBufferByIndex(idx);

        if (!targetBuffer)
          return;

        // 클릭 시점의 버퍼 인덱스 캡처
        uint64_t clickIdx = targetBuffer->getTotalFramesAdded();

        onLogMessage(
            QString(
                "[Recorder] 이벤트 감지 (I:%1): %2초 후 저장을 시작합니다...")
                .arg(clickIdx)
                .arg(postSec));

        QString camId = QString("Ch %1").arg(idx + 1);

        // postSec (미래 프레임)이 쌓일 때까지 대기
        QTimer::singleShot(
            postSec * 1000, this,
            [this, desc, preSec, postSec, idx, camId, targetBuffer,
             clickIdx]() {
              // 실제 스트림 FPS 반영 (preSec + postSec 구간의 프레임 수 계산을
              // 위해)
              double actualFps = m_recordPanelController->getLiveFps();
              if (actualFps <= 0)
                actualFps = 15.0; // 세이프가드

              // 전체 구간(preSec + postSec)에 해당하는 프레임 추출
              // 클릭 시점(clickIdx) 기준으로 전후 구간을 계산
              uint64_t startIdx =
                  (clickIdx > static_cast<uint64_t>(preSec * actualFps))
                      ? (clickIdx - static_cast<uint64_t>(preSec * actualFps))
                      : 0;

              auto frames = targetBuffer->getFramesSince(startIdx);

              // 우리가 필요로 하는 총 프레임 수
              size_t requestedFrames =
                  static_cast<size_t>((preSec + postSec) * actualFps);
              if (frames.size() > requestedFrames) {
                frames.resize(requestedFrames);
              }

              if (frames.empty()) {
                onLogMessage(QString::fromUtf8(
                    "[Recorder] 버퍼에 저장된 프레임이 없습니다."));
                return;
              }

              QString fileName = QString("event_Ch%1_%2.mp4")
                                     .arg(idx + 1)
                                     .arg(QDateTime::currentDateTime().toString(
                                         "yyyyMMdd_HHmmss"));
              QString filePath = QDir(QCoreApplication::applicationDirPath())
                                     .filePath("records/videos/" + fileName);

              QMetaObject::invokeMethod(
                  m_recorderWorker, "saveVideo",
                  Q_ARG(std::vector<QSharedPointer<cv::Mat>>, frames),
                  Q_ARG(QString, filePath),
                  Q_ARG(int, static_cast<int>(actualFps)),
                  Q_ARG(QString, "VIDEO"), Q_ARG(QString, desc),
                  Q_ARG(QString, camId));

              onLogMessage(QString("[Recorder] 이벤트 구간 저장 완료: %1 "
                                   "(%2초 전 ~ %3초 후, FPS: %4, 프레임수: %5)")
                               .arg(fileName)
                               .arg(preSec)
                               .arg(postSec)
                               .arg(actualFps)
                               .arg(frames.size()));
            });
      });

  m_recorderWorker = new MediaRecorderWorker();
  m_recorderThread = new QThread(this);
  m_recorderWorker->moveToThread(m_recorderThread);
  connect(m_recorderThread, &QThread::finished, m_recorderWorker,
          &QObject::deleteLater);
  connect(m_recorderWorker, &MediaRecorderWorker::finished, this,
          &MainWindowController::onMediaSaveFinished);
  connect(m_recorderWorker, &MediaRecorderWorker::error, this,
          [this](const QString &message) {
            onLogMessage(QString("[Recorder] %1").arg(message));
          });
  m_recorderThread->start();

  if (m_telegramApi) {
    connect(m_telegramApi, &TelegramBotAPI::usersUpdated, m_dbPanelController,
            &DbPanelController::refreshUserTable);
  }

  m_resizeDebounceTimer = new QTimer(this);
  m_resizeDebounceTimer->setSingleShot(true);
  connect(m_resizeDebounceTimer, &QTimer::timeout, this,
          &MainWindowController::onVideoWidgetResizedSlot);

  for (VideoWidget *videoWidget : m_ui.videoWidgets) {
    if (videoWidget) {
      videoWidget->installEventFilter(this);
    }
  }
  if (m_ui.recordVideoWidget) {
    m_ui.recordVideoWidget->installEventFilter(this);
  }
  for (int i = 0; i < 4; ++i) {
    if (m_ui.channelCards[i]) {
      m_ui.channelCards[i]->installEventFilter(this);
    }
    if (m_ui.channelNameLabels[i]) {
      m_ui.channelNameLabels[i]->installEventFilter(this);
    }
    if (m_ui.thumbnailLabels[i]) {
      m_ui.thumbnailLabels[i]->installEventFilter(this);
    }
  }

  for (size_t i = 0; i < m_channels.size(); ++i) {
    if (m_channels[i]) {
      m_channels[i]->setReidPanelActive(i == 0);
    }
  }

  initRoiDbForChannels();
  refreshRoiSelectorForTarget();
  updateChannelCardSelection();
  connectSignals();
  bindRecordPreviewSource(m_ui.cmbManualCamera
                              ? m_ui.cmbManualCamera->currentIndex()
                              : 0);
  // 상시 녹화 초기화
  for (int i = 0; i < 4; ++i) {
    // 5 FPS 기준 1분(60초) = 300프레임. 넉넉하게 600으로 설정
    m_continuousBuffers[i] = new VideoBufferManager(600, this);
  }

  m_continuousRecordTimer = new QTimer(this);
  m_continuousRecordTimer->setInterval(60000); // 기본 1분
  connect(m_continuousRecordTimer, &QTimer::timeout, this,
          &MainWindowController::onContinuousRecordTimeout);

  m_cleanupTimer = new QTimer(this);
  m_cleanupTimer->setInterval(60000); // 1분마다 자동 삭제 체크
  connect(m_cleanupTimer, &QTimer::timeout, this,
          &MainWindowController::onCleanupTimeout);

  m_continuousRecordTimer->start();
  m_cleanupTimer->start();

  // 설정값 적용 버튼 연결
  if (m_ui.btnApplyContinuousSetting) {
    connect(m_ui.btnApplyContinuousSetting, &QPushButton::clicked, this,
            &MainWindowController::onApplyContinuousSettingClicked);
  }

}

void MainWindowController::startInitialCctv() {
  QStringList cameraKeys = Config::instance().cameraKeys();
  const int count = std::min(static_cast<int>(m_channels.size()),
                             static_cast<int>(cameraKeys.size()));
  for (int i = 0; i < count; ++i) {
    CameraChannelRuntime *channel = channelAt(i);
    if (channel && channel->selectedCardIndex() != i) {
      onChannelCardClicked(i);
    }
  }
  m_selectedChannelIndex = 0;
  if (m_ui.roiTargetCombo) {
    QSignalBlocker blocker(m_ui.roiTargetCombo);
    m_ui.roiTargetCombo->setCurrentIndex(0);
  }
  onRoiTargetChanged(0);
}

void MainWindowController::onSystemConfigChanged() {
  CameraChannelRuntime *primary = channelAt(0);
  if (!primary || primary->selectedCardIndex() != -1) {
    return;
  }
  onChannelCardClicked(0);
}

bool MainWindowController::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::Resize) {
    for (VideoWidget *videoWidget : m_ui.videoWidgets) {
      if (obj == videoWidget) {
        m_resizeDebounceTimer->start(150);
        break;
      }
    }
    if (obj == m_ui.recordVideoWidget) {
      m_resizeDebounceTimer->start(150);
    }
  }

  if (event->type() == QEvent::MouseButtonPress) {
    for (int i = 0; i < 4; ++i) {
      if (obj == m_ui.channelCards[i] || obj == m_ui.thumbnailLabels[i] ||
          obj == m_ui.channelNameLabels[i]) {
        onChannelCardClicked(i);
        return true;
      }
    }
  }

  return QObject::eventFilter(obj, event);
}

void MainWindowController::onVideoWidgetResizedSlot() {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->handleResizeProfileChange();
    }
  }
  updateRecordPreviewSourceSize();
}

void MainWindowController::shutdown() {
  QElapsedTimer timer;
  timer.start();

  const QString summary = m_logDeduplicator.flushPending();
  if (!summary.isEmpty() && m_ui.logView) {
    m_ui.logView->append(summary);
  }

  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->shutdown();
    }
  }
  bindRecordPreviewSource(-1);
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->stop();
    }
  }
  if (m_rpiPanelController) {
    m_rpiPanelController->shutdown();
  }
  // 백그라운드 워커 삭제
  if (m_recorderThread) {
    m_recorderThread->quit();
    m_recorderThread->wait();
    m_recorderThread = nullptr;
    // m_recorderWorker는 QThread::finished 시그널에 deleteLater 연결됨
  }

  const QString shutdownLog =
      QString("[Shutdown] camera/session stop finished in %1 ms")
          .arg(timer.elapsed());
  qDebug() << shutdownLog;
  if (m_ui.logView) {
    m_ui.logView->append(shutdownLog);
  }

  // 상시 녹화 타이머 중지 및 버퍼 정리
  if (m_continuousRecordTimer) {
    m_continuousRecordTimer->stop();
  }
  if (m_cleanupTimer) {
    m_cleanupTimer->stop();
  }
  for (int i = 0; i < 4; ++i) {
    if (m_continuousBuffers[i]) {
      delete m_continuousBuffers[i];
      m_continuousBuffers[i] = nullptr;
    }
  }
}

void MainWindowController::connectSignals() {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->connectSignals();
    }
  }

  if (m_ui.btnApplyRoi) {
    connect(m_ui.btnApplyRoi, &QPushButton::clicked, this,
            &MainWindowController::onStartRoiDraw);
  }
  if (m_ui.btnFinishRoi) {
    connect(m_ui.btnFinishRoi, &QPushButton::clicked, this,
            &MainWindowController::onCompleteRoiDraw);
  }
  if (m_ui.btnDeleteRoi) {
    connect(m_ui.btnDeleteRoi, &QPushButton::clicked, this,
            &MainWindowController::onDeleteSelectedRoi);
  }
  if (m_ui.roiTargetCombo) {
    connect(m_ui.roiTargetCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindowController::onRoiTargetChanged);
  }
  for (VideoWidget *videoWidget : m_ui.videoWidgets) {
    if (!videoWidget) {
      continue;
    }
    connect(videoWidget, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(videoWidget, &VideoWidget::roiPolygonChanged, this,
            &MainWindowController::onRoiPolygonChanged);
  }
  if (m_ui.reidTable) {
    connect(m_ui.reidTable, &QTableWidget::cellClicked, this,
            &MainWindowController::onReidTableCellClicked);
  }
  if (m_ui.chkShowFps) {
    connect(m_ui.chkShowFps, &QCheckBox::toggled, this, [this](bool checked) {
      for (CameraChannelRuntime *channel : m_channels) {
        if (channel) {
          channel->setShowFps(checked);
        }
      }
    });
    for (CameraChannelRuntime *channel : m_channels) {
      if (channel) {
        channel->setShowFps(m_ui.chkShowFps->isChecked());
      }
    }
  }

  if (m_ui.btnSendEntry) {
    connect(m_ui.btnSendEntry, &QPushButton::clicked, this,
            &MainWindowController::onSendEntry);
  }
  if (m_ui.btnSendExit) {
    connect(m_ui.btnSendExit, &QPushButton::clicked, this,
            &MainWindowController::onSendExit);
  }

  connect(m_telegramApi, &TelegramBotAPI::logMessage, this,
          &MainWindowController::onTelegramLog);
  connect(m_telegramApi, &TelegramBotAPI::usersUpdated, this,
          &MainWindowController::onUsersUpdated);
  connect(m_telegramApi, &TelegramBotAPI::paymentConfirmed, this,
          &MainWindowController::onPaymentConfirmed);
  connect(m_telegramApi, &TelegramBotAPI::adminSummoned, this,
          &MainWindowController::onAdminSummoned);

  if (m_rpiPanelController) {
    m_rpiPanelController->connectSignals();
  }

  if (m_dbPanelController) {
    m_dbPanelController->connectSignals();
    m_dbPanelController->refreshAll();
  }

  // 수동 캡처/녹화 버튼 연결 (라이브 탭 + 녹화조회 탭 전용 버튼)
  if (m_ui.btnCaptureManual) {
    connect(m_ui.btnCaptureManual, &QPushButton::clicked, this,
            &MainWindowController::onCaptureManual);
  }
  if (m_ui.btnCaptureRecordTab) {
    connect(m_ui.btnCaptureRecordTab, &QPushButton::clicked, this,
            &MainWindowController::onCaptureManual);
  }

  if (m_ui.btnRecordManual) {
    connect(m_ui.btnRecordManual, &QPushButton::toggled, this,
            &MainWindowController::onRecordManualToggled);
  }
  if (m_ui.btnRecordRecordTab) {
    connect(m_ui.btnRecordRecordTab, &QPushButton::toggled, this,
            &MainWindowController::onRecordManualToggled);
  }
  if (m_ui.cmbManualCamera) {
    connect(m_ui.cmbManualCamera,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindowController::bindRecordPreviewSource);
  }
}

void MainWindowController::refreshZoneTableAllChannels() {
  if (m_dbPanelController) {
    m_dbPanelController->refreshZoneTable();
  }
}

void MainWindowController::initRoiDb() { initRoiDbForChannels(); }

void MainWindowController::initRoiDbForChannels() {
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->reloadRoi(false);
    }
  }
}

void MainWindowController::reloadRoiForTarget(RoiTarget target, bool writeLog) {
  CameraChannelRuntime *channel = channelAt(static_cast<int>(target));
  if (!channel) {
    return;
  }
  channel->reloadRoi(writeLog);
}

void MainWindowController::refreshRoiSelectorForTarget() {
  if (!m_ui.roiSelectorCombo) {
    return;
  }
  m_ui.roiSelectorCombo->clear();
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

  const RoiService *service = roiServiceForTarget(m_roiTarget);
  if (!service) {
    return;
  }

  const QVector<QJsonObject> &records = service->records();
  for (int i = 0; i < records.size(); ++i) {
    const QJsonObject &record = records[i];
    const QString name =
        record["zone_name"].toString(QString("zone_%1").arg(i + 1));
    m_ui.roiSelectorCombo->addItem(name, i);
  }
}

void MainWindowController::appendRoiStructuredLog(const QJsonObject &roiData) {
  if (!m_ui.logView) {
    return;
  }
  const QString line =
      QString::fromUtf8(QJsonDocument(roiData).toJson(QJsonDocument::Compact));
  qDebug().noquote() << line;
  m_ui.logView->append(line);
}

void MainWindowController::initChannelCards() {
  if (!Config::instance().load()) {
    onLogMessage("Warning: could not reload config; using existing values.");
  }

  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  for (int i = 0; i < 4; ++i) {
    if (!m_ui.channelCards[i]) {
      continue;
    }

    const bool isNoSignal = (i >= cameraKeys.size());
    m_ui.channelCards[i]->setVisible(true);

    if (m_ui.channelNameLabels[i]) {
      m_ui.channelNameLabels[i]->setText(QString("Ch%1").arg(i + 1));
    }

    if (m_ui.thumbnailLabels[i]) {
      m_ui.thumbnailLabels[i]->setPixmap(QPixmap());
      m_ui.thumbnailLabels[i]->setText(isNoSignal ? QStringLiteral("NO SIGNAL")
                                                  : QStringLiteral("STANDBY"));
      if (isNoSignal) {
        m_ui.thumbnailLabels[i]->setStyleSheet(
            "background: #0a0a1a; color: #555; border-radius: 4px; "
            "font-weight: bold; font-size: 10px;");
      }
    }

    if (m_ui.channelStatusDots[i]) {
      m_ui.channelStatusDots[i]->setStyleSheet(
          isNoSignal
              ? "background: #ef4444; border-radius: 5px; border: none;"
              : "background: #10b981; border-radius: 5px; border: none;");
    }
  }

  for (CameraChannelRuntime *channel : m_channels) {
    if (channel && channel->videoWidget()) {
      channel->videoWidget()->setVisible(false);
    }
  }
}

void MainWindowController::updateChannelCardSelection() {
  QStringList cameraKeys = Config::instance().cameraKeys();

  for (int i = 0; i < 4; ++i) {
    CameraChannelRuntime *channel = channelAt(i);
    if (m_ui.channelCards[i]) {
      const bool isSelected = channel && channel->selectedCardIndex() == i;
      m_ui.channelCards[i]->setProperty("selected", isSelected);
      m_ui.channelCards[i]->style()->unpolish(m_ui.channelCards[i]);
      m_ui.channelCards[i]->style()->polish(m_ui.channelCards[i]);
    }
    if (m_ui.channelStatusDots[i]) {
      const bool hasSignal = i < cameraKeys.size();
      const CameraSource *source = sourceAt(i);

      QString style;
      if (!hasSignal) {
        style = QStringLiteral(
            "background: #ef4444; border-radius: 5px; border: none;");
      } else if (!source) {
        style = QStringLiteral(
            "background: #666; border-radius: 5px; border: none;");
      } else {
        switch (source->status()) {
        case CameraSource::Status::Live:
          style = (channel && channel->selectedCardIndex() == i)
                      ? QStringLiteral("background: #00e676; border-radius: "
                                       "5px; border: none;")
                      : QStringLiteral("background: #10b981; border-radius: "
                                       "5px; border: none;");
          break;
        case CameraSource::Status::Connecting:
          style = QStringLiteral(
              "background: #f59e0b; border-radius: 5px; border: none;");
          break;
        case CameraSource::Status::Error:
          style = QStringLiteral(
              "background: #ef4444; border-radius: 5px; border: none;");
          break;
        case CameraSource::Status::Stopped:
        default:
          style = QStringLiteral(
              "background: #666; border-radius: 5px; border: none;");
          break;
        }
      }
      m_ui.channelStatusDots[i]->setStyleSheet(style);
    }
  }
}

void MainWindowController::startCameraSources() {
  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  for (int i = 0; i < static_cast<int>(m_cameraSources.size()); ++i) {
    if (i >= cameraKeys.size()) {
      continue;
    }
    if (m_cameraSources[static_cast<size_t>(i)]) {
      continue;
    }

    CameraSource *source = new CameraSource(cameraKeys[i], i, this);
    source->initialize(m_telegramApi);
    connect(source, &CameraSource::rawFrameReady, this,
            &MainWindowController::onRawFrameReady);
    connect(source, &CameraSource::thumbnailFrameReady, this,
            &MainWindowController::updateThumbnailForCard);
    connect(source, &CameraSource::statusChanged, this,
            [this](int, CameraSource::Status, const QString &) {
              updateChannelCardSelection();
            });
    connect(source, &CameraSource::logMessage, this,
            &MainWindowController::onLogMessage);
    m_cameraSources[static_cast<size_t>(i)] = source;

    QPointer<CameraSource> deferredSource(source);
    const int startDelayMs = i * kCameraStartStaggerMs;
    QTimer::singleShot(startDelayMs, this,
                       [this, deferredSource, i, startDelayMs]() {
      if (!deferredSource) {
        return;
      }
      onLogMessage(QString("[Camera] Ch %1 시작 예약 실행 (%2 ms)")
                       .arg(i + 1)
                       .arg(startDelayMs));
      deferredSource->start();
    });
  }
}

void MainWindowController::bindRecordPreviewSource(int index) {
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

  CameraSource *source = sourceAt(index);
  if (!source) {
    return;
  }

  m_recordPreviewSource = source;
  m_recordPreviewConnection = connect(
      source, &CameraSource::displayFrameReady, this,
      [this](const QImage &image, const QList<ObjectInfo> &) {
        if (m_recordPanelController) {
          m_recordPanelController->updateLiveFrame(image);
        }
      });
  source->attachDisplayConsumer(kRecordPreviewConsumerId,
                                m_ui.recordVideoWidget
                                    ? m_ui.recordVideoWidget->size()
                                    : QSize());
}

void MainWindowController::updateRecordPreviewSourceSize() {
  if (!m_recordPreviewSource || !m_ui.recordVideoWidget) {
    return;
  }

  m_recordPreviewSource->updateConsumerSize(kRecordPreviewConsumerId,
                                            m_ui.recordVideoWidget->size());
}

void MainWindowController::onRoiTargetChanged(int index) {
  if (index < 0 || index >= static_cast<int>(m_channels.size())) {
    m_roiTarget = RoiTarget::Ch1;
  } else {
    m_roiTarget = static_cast<RoiTarget>(index);
  }
  for (int i = 0; i < static_cast<int>(m_channels.size()); ++i) {
    if (m_channels[static_cast<size_t>(i)]) {
      m_channels[static_cast<size_t>(i)]->setReidPanelActive(
          i == static_cast<int>(m_roiTarget));
    }
  }
  refreshRoiSelectorForTarget();
  onLogMessage(QString("[ROI] 편집 대상 변경: %1").arg(roiTargetLabel(m_roiTarget)));
}

void MainWindowController::onChannelCardClicked(int index) {
  if (index < 0 || index >= 4) {
    return;
  }

  CameraChannelRuntime *channel = channelAt(index);
  if (!channel) {
    return;
  }
  m_selectedChannelIndex = index;

  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  const bool isNoSignal = (index >= cameraKeys.size());
  CameraSource *newSource = isNoSignal ? nullptr : sourceAt(index);

  auto clearThumbnail = [this, &cameraKeys](int cardIndex) {
    if (cardIndex < 0 || cardIndex >= 4 || !m_ui.thumbnailLabels[cardIndex]) {
      return;
    }
    if (isCameraSourceRunning(cardIndex)) {
      return;
    }
    m_ui.thumbnailLabels[cardIndex]->setPixmap(QPixmap());
    const bool hasSignal = cardIndex < cameraKeys.size();
    m_ui.thumbnailLabels[cardIndex]->setText(
        hasSignal ? QStringLiteral("STANDBY") : QStringLiteral("NO SIGNAL"));
  };

  if (m_ui.roiTargetCombo && m_ui.roiTargetCombo->currentIndex() != index) {
    QSignalBlocker blocker(m_ui.roiTargetCombo);
    m_ui.roiTargetCombo->setCurrentIndex(index);
  }
  onRoiTargetChanged(index);

  if (index == channel->selectedCardIndex()) {
    channel->deactivate();
    updateChannelCardSelection();
    onLogMessage(QString("[Camera] Ch %1 해제").arg(index + 1));
    clearThumbnail(index);
    refreshZoneTableAllChannels();
    return;
  }

  if (isNoSignal) {
    channel->selectCardWithoutStream(index);
    updateChannelCardSelection();
    onLogMessage(QString("[Camera] Ch %1: 신호 없음").arg(index + 1));
    return;
  }

  onLogMessage(
      QString("[Camera] Ch %1 켜기: %2")
          .arg(index + 1)
          .arg(newSource ? newSource->cameraKey() : QStringLiteral("N/A")));
  if (!channel->activate(newSource, index)) {
    updateChannelCardSelection();
    return;
  }
  updateChannelCardSelection();
  refreshZoneTableAllChannels();
}

void MainWindowController::updateObjectFilter(
    const QSet<QString> &disabledTypes) {
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->updateObjectFilter(disabledTypes);
    }
  }
}

void MainWindowController::onLogMessage(const QString &msg) {
  if (!m_ui.logView) {
    return;
  }

  bool showInUi = true;
  if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
    if (msg.startsWith(QStringLiteral("[Parking]"))) {
      showInUi = false;
    }
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const LogDeduplicator::IngestResult ingestResult =
      m_logDeduplicator.ingest(msg, nowMs);

  if (!ingestResult.flushSummary.isEmpty() && showInUi) {
    m_ui.logView->append(ingestResult.flushSummary);
  }
  if (ingestResult.suppressed) {
    return;
  }

  if (showInUi) {
    m_ui.logView->append(msg);
  }

  if (m_ui.eventListWidget) {
    static const QStringList eventPrefixes = {"[Camera]", "[Parking]", "[OCR]",
                                              "[ROI]",    "[A]",       "[B]"};
    bool isEvent = false;
    for (const QString &prefix : eventPrefixes) {
      if (msg.contains(prefix)) {
        isEvent = true;
        break;
      }
    }
    if (isEvent) {
      const QString timestamp =
          QDateTime::currentDateTime().toString("HH:mm:ss");
      const QString eventText = QString("[%1] %2").arg(timestamp, msg);
      m_ui.eventListWidget->insertItem(0, eventText);
      while (m_ui.eventListWidget->count() > 100) {
        delete m_ui.eventListWidget->takeItem(m_ui.eventListWidget->count() -
                                              1);
      }
    }
  }
}

void MainWindowController::onStartRoiDraw() {
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  if (!targetWidget || !m_ui.logView) {
    return;
  }
  targetWidget->startRoiDrawing();
  m_ui.logView->append(
      QString("[ROI] Draw mode (%1): left-click points, then press 'ROI 완료'.")
          .arg(roiTargetLabel(m_roiTarget)));
}

void MainWindowController::onCompleteRoiDraw() {
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  RoiService *targetService = roiServiceForTarget(m_roiTarget);
  if (!targetWidget || !targetService || !m_ui.logView) {
    return;
  }
  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  if (auto nameError = targetService->isValidName(typedName);
      nameError.has_value()) {
    m_ui.logView->append(QString("[ROI] 완료 실패: %1").arg(nameError.value()));
    return;
  }
  if (targetService->isDuplicateName(typedName)) {
    m_ui.logView->append(
        QString("[ROI] 완료 실패: 이름 '%1' 이(가) 이미 존재합니다.")
            .arg(typedName));
    return;
  }

  if (!targetWidget->completeRoiDrawing()) {
    m_ui.logView->append("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
  }
}

void MainWindowController::onDeleteSelectedRoi() {
  CameraChannelRuntime *targetChannel =
      channelAt(static_cast<int>(m_roiTarget));
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  RoiService *targetService = roiServiceForTarget(m_roiTarget);
  if (!m_ui.roiSelectorCombo || !targetChannel || !targetWidget ||
      !targetService || !m_ui.logView) {
    return;
  }
  if (m_ui.roiSelectorCombo->currentIndex() < 0) {
    return;
  }
  const int recordIndex = m_ui.roiSelectorCombo->currentData().toInt();
  if (recordIndex < 0 || recordIndex >= targetService->count()) {
    m_ui.logView->append("[ROI] 삭제 실패: ROI를 선택해주세요.");
    return;
  }

  const Result<QString> deleteResult = targetService->removeAt(recordIndex);
  if (!deleteResult.isOk()) {
    m_ui.logView->append(
        QString("[ROI][DB] 삭제 실패: %1").arg(deleteResult.error));
    return;
  }
  if (!targetWidget->removeRoiAt(recordIndex)) {
    m_ui.logView->append(
        "[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
    return;
  }

  targetChannel->syncEnabledRoiPolygons();
  refreshRoiSelectorForTarget();
  refreshZoneTableAllChannels();

  int nextRecordIndex = recordIndex;
  if (nextRecordIndex >= targetService->count()) {
    nextRecordIndex = targetService->count() - 1;
  }
  const int comboIndex = (nextRecordIndex >= 0)
                             ? m_ui.roiSelectorCombo->findData(nextRecordIndex)
                             : -1;
  m_ui.roiSelectorCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
  m_ui.logView->append(QString("[ROI] 삭제 완료: %1").arg(deleteResult.data));
}

void MainWindowController::onRoiChanged(const QRect &roi) {
  if (!m_ui.logView) {
    return;
  }
  const VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  QString channel = QStringLiteral("Unknown");
  for (int i = 0; i < 4; ++i) {
    if (sourceWidget == m_ui.videoWidgets[i]) {
      channel = QString("Ch%1").arg(i + 1);
      break;
    }
  }
  m_ui.logView->append(QString("[ROI][%1] bbox x:%2 y:%3 w:%4 h:%5")
                           .arg(channel)
                           .arg(roi.x())
                           .arg(roi.y())
                           .arg(roi.width())
                           .arg(roi.height()));
}

void MainWindowController::onRoiPolygonChanged(const QPolygon &polygon,
                                               const QSize &frameSize) {
  if (!m_ui.logView) {
    return;
  }
  if (frameSize.isEmpty()) {
    m_ui.logView->append("[ROI] 저장 실패: 프레임 크기가 유효하지 않습니다.");
    return;
  }

  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();

  VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  RoiTarget target = m_roiTarget;
  for (int i = 0; i < 4; ++i) {
    if (sourceWidget == m_ui.videoWidgets[i]) {
      target = static_cast<RoiTarget>(i);
      break;
    }
  }

  CameraChannelRuntime *targetChannel = channelAt(static_cast<int>(target));
  RoiService *targetService = roiServiceForTarget(target);
  VideoWidget *targetWidget = videoWidgetForTarget(target);
  if (!targetChannel || !targetService || !targetWidget) {
    return;
  }

  const Result<QJsonObject> createResult =
      targetService->createFromPolygon(polygon, frameSize, typedName);
  if (!createResult.isOk()) {
    if (targetWidget->roiCount() > 0) {
      targetWidget->removeRoiAt(targetWidget->roiCount() - 1);
    }
    m_ui.logView->append(
        QString("[ROI][DB] 저장 실패: %1").arg(createResult.error));
    if (target == m_roiTarget) {
      refreshRoiSelectorForTarget();
    }
    return;
  }

  if (target == m_roiTarget) {
    refreshRoiSelectorForTarget();
  }
  refreshZoneTableAllChannels();
  if (target == m_roiTarget && m_ui.roiSelectorCombo) {
    m_ui.roiSelectorCombo->setCurrentIndex(m_ui.roiSelectorCombo->count() - 1);
  }
  targetChannel->syncEnabledRoiPolygons();
  const int recordIndex = targetService->count() - 1;
  targetWidget->setRoiLabelAt(
      recordIndex, createResult.data["zone_name"].toString().trimmed());
  appendRoiStructuredLog(createResult.data);
}

void MainWindowController::onRawFrameReady(int cardIndex,
                                           QSharedPointer<cv::Mat> framePtr,
                                           qint64 timestampMs) {
  if (cardIndex < 0 || cardIndex >= 4 || !framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > 100) {
    return;
  }

  // 상시녹화 버퍼 추가 (5 FPS 유지)
  if (m_continuousBuffers[cardIndex]) {
    if (!m_continuousThrottleTimers[cardIndex].isValid() ||
        m_continuousThrottleTimers[cardIndex].elapsed() >= 200) {
      m_continuousBuffers[cardIndex]->addFrame(framePtr);
      m_continuousThrottleTimers[cardIndex].restart();
    }
  }

  // 매뉴얼 캡쳐/이벤트 구간 저장을 위한 버퍼 추가
  VideoBufferManager *targetBuffer = getBufferByIndex(cardIndex);
  if (targetBuffer) {
    targetBuffer->addFrame(framePtr);
  }
}
void MainWindowController::onSendEntry() {
  if (!m_ui.entryPlateInput || !m_ui.logView) {
    return;
  }

  const QString plate = m_ui.entryPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    m_ui.logView->append("[Telegram] 차량번호를 입력해주세요.");
    return;
  }
  m_telegramApi->sendEntryNotice(plate);
}

void MainWindowController::onSendExit() {
  if (!m_ui.exitPlateInput || !m_ui.feeInput || !m_ui.logView) {
    return;
  }

  const QString plate = m_ui.exitPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    m_ui.logView->append("[Telegram] 차량번호를 입력해주세요.");
    return;
  }
  m_telegramApi->sendExitNotice(plate, m_ui.feeInput->value());
}

void MainWindowController::onTelegramLog(const QString &msg) {
  if (m_ui.logView) {
    m_ui.logView->append(msg);
  }
}

void MainWindowController::onUsersUpdated(int count) {
  if (m_ui.userCountLabel) {
    m_ui.userCountLabel->setText(QString("%1 명").arg(count));
  }

  if (m_ui.userTable && m_telegramApi) {
    const QMap<QString, QString> users = m_telegramApi->getRegisteredUsers();
    m_ui.userTable->setRowCount(0);
    for (auto it = users.begin(); it != users.end(); ++it) {
      const int row = m_ui.userTable->rowCount();
      m_ui.userTable->insertRow(row);
      m_ui.userTable->setItem(row, 0, new QTableWidgetItem(it.key()));
      m_ui.userTable->setItem(row, 1, new QTableWidgetItem(it.value()));
    }
  }

  if (m_dbPanelController) {
    m_dbPanelController->refreshUserTable();
  }
}

void MainWindowController::onPaymentConfirmed(const QString &plate,
                                              int amount) {
  if (m_ui.logView) {
    const QString msg =
        QString("[Payment] 💰 결제 완료 수신! 차량: %1, 금액: %2원")
            .arg(plate)
            .arg(amount);

    if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
      return;
    }
    m_ui.logView->append(msg);
  }
}

void MainWindowController::onReidTableCellClicked(int row, int column) {
  Q_UNUSED(column);
  if (!m_ui.reidTable) {
    return;
  }

  QTableWidgetItem *idItem = m_ui.reidTable->item(row, 0);
  QTableWidgetItem *plateItem = m_ui.reidTable->item(row, 2);

  if (idItem && m_ui.forceObjectIdInput) {
    m_ui.forceObjectIdInput->setValue(idItem->text().toInt());
  }

  QTableWidgetItem *typeItem = m_ui.reidTable->item(row, 1);
  if (typeItem && m_ui.forceTypeInput) {
    m_ui.forceTypeInput->setText(typeItem->text());
  }

  if (plateItem && m_ui.forcePlateInput) {
    m_ui.forcePlateInput->setText(plateItem->text());
  }

  QTableWidgetItem *scoreItem = m_ui.reidTable->item(row, 3);
  if (scoreItem && m_ui.forceScoreInput) {
    m_ui.forceScoreInput->setValue(scoreItem->text().toDouble());
  }

  QTableWidgetItem *bboxItem = m_ui.reidTable->item(row, 4);
  if (bboxItem && m_ui.forceBBoxInput) {
    m_ui.forceBBoxInput->setText(bboxItem->text());
  }
}

void MainWindowController::onAdminSummoned(const QString &chatId,
                                           const QString &name) {
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[알림] 🚨 관리자 호출 수신! (User: %1, ChatID: %2)")
            .arg(name, chatId));
  }

  QMessageBox *box = new QMessageBox(nullptr);
  box->setWindowTitle("관리자 호출");
  box->setText(
      QString("🚨 사용자가 관리자를 호출했습니다!\n\n이름: %1\nChat ID: %2")
          .arg(name, chatId));
  box->setIcon(QMessageBox::Warning);
  box->setStandardButtons(QMessageBox::Ok);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setWindowModality(Qt::NonModal);
  box->show();
}

void MainWindowController::onCaptureManual() {
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

  VideoBufferManager *targetBuffer = getBufferByIndex(idx);
  QString camId = QString("Ch %1").arg(idx + 1);

  onLogMessage(QString("[Recorder] [%1] 수동 캐캐 요청...").arg(camId));

  if (!targetBuffer) {
    onLogMessage(QString("[Recorder] [%1] 버퍼 객체가 없습니다.").arg(camId));
    return;
  }

  auto frames = targetBuffer->getFrames();
  onLogMessage(QString("[Recorder] [%1] 버퍼 프레임 수: %2")
                   .arg(camId)
                   .arg(frames.size()));

  if (frames.empty()) {
    onLogMessage(QString("[Recorder] [%1] 버퍼가 비어있습니다. 해당 카메라가 "
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

  onLogMessage(
      QString("[Recorder] [%1] 캐캐 저장 코: %2").arg(camId, fileName));
}

void MainWindowController::onRecordManualToggled(bool checked) {
  // 라이브 뷰의 버튼과 녹화조회 탭의 버튼 상태를 동기화
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
      btn->setStyleSheet(""); // 기본 스타일로 복구
    }
  };

  updateButtonStyle(m_ui.btnRecordManual, checked);
  updateButtonStyle(m_ui.btnRecordRecordTab, checked);

  // No status label to update

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
    VideoBufferManager *buf = getBufferByIndex(idx);
    m_manualRecordStartIdx = buf ? buf->getTotalFramesAdded() : 0;

    QString camId = QString("Ch %1").arg(idx + 1);
    onLogMessage(QString("[Recorder] [%1] 수동 녹화 시작 (시작 인덱스: %2)")
                     .arg(camId)
                     .arg(m_manualRecordStartIdx));
  } else {
    int idx = m_manualRecordChannelIdx;
    VideoBufferManager *targetBuffer = getBufferByIndex(idx);
    QString camId = QString("Ch %1").arg(idx + 1);

    onLogMessage(
        QString("[Recorder] [%1] 녹화 중지 요청 - 저장 중...").arg(camId));

    if (!targetBuffer) {
      onLogMessage(QString("[Recorder] [%1] 버퍼 객체가 없습니다.").arg(camId));
      m_isManualRecording = false;
      return;
    }
    m_isManualRecording = false;

    auto frames = targetBuffer->getFramesSince(m_manualRecordStartIdx);
    onLogMessage(QString("[Recorder] [%1] 버퍼 프레임 수: %2")
                     .arg(camId)
                     .arg(frames.size()));

    if (frames.empty()) {
      onLogMessage(QString("[Recorder] [%1] 버퍼가 비어있습니다. 해당 카메라가 "
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

    onLogMessage(QString("[Recorder] [%1] 녹화 파일 저장 실행: %2")
                     .arg(camId, fileName));
  }
}

void MainWindowController::onMediaSaveFinished(bool success,
                                               const QString &filePath,
                                               const QString &type,
                                               const QString &description,
                                               const QString &cameraId) {
  if (!success) {
    onLogMessage(QString("[Recorder] 미디어 저장 실패: %1").arg(filePath));
    return;
  }

  QString fileName = QFileInfo(filePath).fileName();

  // 주 스레드에서 안전하게 DB 기록
  if (m_mediaRepo) {
    m_mediaRepo->addMediaRecord(type, description, cameraId, filePath);
  }

  onLogMessage(QString("[Recorder] 미디어 저장 완료: %1").arg(fileName));

  // 목록 자동 갱신
  if (m_recordPanelController) {
    m_recordPanelController->refreshLogTable();
  }
}

void MainWindowController::onContinuousRecordTimeout() {
  // 활성화 체크박스 제거로 상시 실행
  int intervalMin = 1; // 1분 고정
  QString camId;

  for (int i = 0; i < 4; ++i) {
    if (!m_continuousBuffers[i])
      continue;

    // 5 FPS 기준 1분(60초) = 300프레임
    auto frames = m_continuousBuffers[i]->getFrames(0, intervalMin * 60, 5);
    if (frames.empty())
      continue;

    camId = QString("Ch %1").arg(i + 1);
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

  // 상시녹화 파일 생성 후 즉시 오래된 파일 정리 수행 (UX 향상)
  onCleanupTimeout();
}

void MainWindowController::onApplyContinuousSettingClicked() {
  onLogMessage(QString("[System] 상시녹화 설정 적용: 보존기간 %1분")
                   .arg(m_ui.spinRecordRetention->value()));
  // 즉시 삭제 체크 트리거
  onCleanupTimeout();
}

void MainWindowController::onCleanupTimeout() {
  // 보존 기간을 분 단위로 해석
  int retentionMinutes =
      m_ui.spinRecordRetention ? m_ui.spinRecordRetention->value() : 60;
  if (!m_mediaRepo)
    return;

  QString error;
  auto oldRecords =
      m_mediaRepo->getOldMediaRecordsByMinutes(retentionMinutes, &error);

  if (!error.isEmpty()) {
    onLogMessage(QString("[Recorder] DB 조회 오류: %1").arg(error));
    return;
  }

  int deleteCount = 0;
  int failCount = 0;

  for (const auto &record : oldRecords) {
    // 상시녹화(CONTINUOUS) 타입만 자동 삭제 대상으로 지정
    if (record["type"].toString() != "CONTINUOUS")
      continue;

    int id = record["id"].toInt();
    QString path = record["file_path"].toString();

    if (QFile::remove(path)) {
      m_mediaRepo->deleteMediaRecord(id);
      deleteCount++;
    } else {
      if (!QFile::exists(path)) {
        // 파일이 이미 없는 경우 DB에서도 제거
        m_mediaRepo->deleteMediaRecord(id);
        deleteCount++;
      } else {
        // 파일이 존재하지만 삭제 실패 (잠김 상태 등)
        failCount++;
        qWarning() << "[Recorder] 파일 삭제 실패 (잠김 예상):" << path;
      }
    }
  }

  if (deleteCount > 0) {
    onLogMessage(QString("[Recorder] 상시녹화 오래된 파일 %1개 자동 정리 완료")
                     .arg(deleteCount));
    if (m_recordPanelController) {
      m_recordPanelController->refreshLogTable();
    }
  }

  if (failCount > 0) {
    onLogMessage(
        QString(
            "[Recorder] 상시녹화 파일 %1개를 삭제하지 못했습니다. (사용 중)")
            .arg(failCount));
  }
}

void MainWindowController::onContinuousSettingChanged() {
  // spinRecordInterval 제거로 인해 더 이상 사용하지 않음
}

VideoBufferManager *MainWindowController::getBufferByIndex(int index) const {
  switch (index) {
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

CameraChannelRuntime *MainWindowController::channelAt(int index) const {
  if (index < 0 || index >= static_cast<int>(m_channels.size())) {
    return nullptr;
  }
  return m_channels[static_cast<size_t>(index)];
}

CameraSource *MainWindowController::sourceAt(int cardIndex) const {
  if (cardIndex < 0 || cardIndex >= static_cast<int>(m_cameraSources.size())) {
    return nullptr;
  }
  return m_cameraSources[static_cast<size_t>(cardIndex)];
}

VideoWidget *
MainWindowController::videoWidgetForTarget(RoiTarget target) const {
  CameraChannelRuntime *channel = channelAt(static_cast<int>(target));
  return channel ? channel->videoWidget() : nullptr;
}

RoiService *MainWindowController::roiServiceForTarget(RoiTarget target) {
  CameraChannelRuntime *channel = channelAt(static_cast<int>(target));
  return channel ? channel->roiService() : nullptr;
}

const RoiService *
MainWindowController::roiServiceForTarget(RoiTarget target) const {
  CameraChannelRuntime *channel = channelAt(static_cast<int>(target));
  return channel ? channel->roiService() : nullptr;
}

ParkingService *
MainWindowController::parkingServiceForTarget(RoiTarget target) {
  CameraChannelRuntime *channel = channelAt(static_cast<int>(target));
  return channel ? channel->parkingService() : nullptr;
}

QString MainWindowController::cameraKeyForTarget(RoiTarget target) const {
  CameraChannelRuntime *channel = channelAt(static_cast<int>(target));
  return channel ? channel->cameraKey() : QString();
}

QString MainWindowController::roiTargetLabel(RoiTarget target) const {
  return QString("Ch%1").arg(static_cast<int>(target) + 1);
}

void MainWindowController::updateThumbnailForCard(int cardIndex,
                                                  const QImage &image) {
  if (cardIndex < 0 || cardIndex >= 4 || image.isNull() ||
      !m_ui.thumbnailLabels[cardIndex]) {
    return;
  }

  if (m_renderTimerThumbs[cardIndex].isValid() &&
      m_renderTimerThumbs[cardIndex].elapsed() < 200) {
    return;
  }
  m_renderTimerThumbs[cardIndex].restart();

  const QSize targetSize = m_ui.thumbnailLabels[cardIndex]->size();
  const QImage scaledImg =
      image.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
  const QPixmap pixmap = QPixmap::fromImage(scaledImg);
  m_ui.thumbnailLabels[cardIndex]->setText(QString());
  QMetaObject::invokeMethod(m_ui.thumbnailLabels[cardIndex], "setPixmap",
                            Qt::QueuedConnection, Q_ARG(QPixmap, pixmap));
}

bool MainWindowController::isCameraSourceRunning(int cardIndex) const {
  if (cardIndex < 0 || cardIndex >= static_cast<int>(m_cameraSources.size())) {
    return false;
  }

  CameraSource *source = m_cameraSources[static_cast<size_t>(cardIndex)];
  return source && source->isRunning();
}
