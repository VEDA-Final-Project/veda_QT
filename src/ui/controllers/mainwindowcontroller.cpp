#include "mainwindowcontroller.h"

#include "camera/camerasource.h"
#include "camerachannelruntime.h"
#include "config/config.h"
#include "database/databasecontext.h"
#include "database/mediarepository.h"
#include "dbpanelcontroller.h"
#include "mediacapturecontroller.h"
#include "parking/parkingservice.h"
#include "recordpanelcontroller.h"
#include "roi/roiservice.h"
#include "roipanelcontroller.h"
#include "rpipanelcontroller.h"
#include "telegram/telegrambotapi.h"
#include "telegrampanelcontroller.h"
#include "ui/video/videowidget.h"
#include "video/videobuffermanager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QThread>
#include <QTimer>

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
      CameraChannelRuntime::Slot::Primary, QStringLiteral("A"),
      m_ui.videoWidgetPrimary, channelUiRefs, this);
  m_channels[1] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Secondary, QStringLiteral("B"),
      m_ui.videoWidgetSecondary, channelUiRefs, this);

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

  // ── RPi Panel Controller ──
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

  // ── Database ──
  const QString dbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/veda.db");
  DatabaseContext::init(dbPath);

  initChannelCards();
  startCameraSources();

  // ── DB Panel Controller ──
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
    int target = m_roiPanelController ? m_roiPanelController->roiTarget() : 0;
    return parkingServiceForTarget(static_cast<RoiTarget>(target));
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

  // ── Media Repository ──
  m_mediaRepo = new MediaRepository();
  m_mediaRepo->init();

  // ── Record Panel Controller ──
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
  recordUiRefs.btnVideoPlay = m_ui.btnVideoPlay;
  recordUiRefs.btnVideoPause = m_ui.btnVideoPause;
  recordUiRefs.btnVideoStop = m_ui.btnVideoStop;
  recordUiRefs.videoSeekSlider = m_ui.videoSeekSlider;
  recordUiRefs.videoTimeLabel = m_ui.videoTimeLabel;
  recordUiRefs.cmbManualCamera = m_ui.cmbManualCamera;
  recordUiRefs.spinRecordRetention = m_ui.spinRecordRetention;
  recordUiRefs.lblContinuousStatus = m_ui.lblContinuousStatus;
  recordUiRefs.btnViewContinuous = m_ui.btnViewContinuous;

  m_recordPanelController =
      new RecordPanelController(recordUiRefs, m_mediaRepo, this);
  m_recordPanelController->connectSignals();
  m_recordPanelController->refreshLogTable();
  m_recordPanelController->setChannelKeys(
      QStringList{QStringLiteral("camera"), QStringLiteral("camera2"),
                  QStringLiteral("camera3"), QStringLiteral("camera4")});

  // ── Media Capture Controller ──
  MediaCaptureController::UiRefs mediaCaptureUiRefs;
  mediaCaptureUiRefs.btnCaptureManual = m_ui.btnCaptureManual;
  mediaCaptureUiRefs.btnRecordManual = m_ui.btnRecordManual;
  mediaCaptureUiRefs.btnCaptureRecordTab = m_ui.btnCaptureRecordTab;
  mediaCaptureUiRefs.btnRecordRecordTab = m_ui.btnRecordRecordTab;
  mediaCaptureUiRefs.cmbManualCamera = m_ui.cmbManualCamera;
  mediaCaptureUiRefs.spinRecordRetention = m_ui.spinRecordRetention;
  mediaCaptureUiRefs.btnApplyContinuousSetting = m_ui.btnApplyContinuousSetting;

  MediaCaptureController::Context mediaCaptureCtx;
  mediaCaptureCtx.getLiveFps = [this]() -> double {
    return m_recordPanelController ? m_recordPanelController->getLiveFps()
                                   : 15.0;
  };

  m_mediaCaptureController = new MediaCaptureController(
      mediaCaptureUiRefs, m_mediaRepo, mediaCaptureCtx, this);

  // Share buffers with RecordPanelController
  m_recordPanelController->setVideoBuffers(
      m_mediaCaptureController->buffer(0), m_mediaCaptureController->buffer(1),
      m_mediaCaptureController->buffer(2), m_mediaCaptureController->buffer(3));

  // Event record: RecordPanel → MediaCaptureController
  connect(m_recordPanelController, &RecordPanelController::eventRecordRequested,
          m_mediaCaptureController,
          &MediaCaptureController::onEventRecordRequested);

  // Media saved: refresh record log table
  connect(m_mediaCaptureController, &MediaCaptureController::mediaSaved,
          m_recordPanelController, &RecordPanelController::refreshLogTable);

  // Log messages from MediaCaptureController → central log handler
  connect(m_mediaCaptureController, &MediaCaptureController::logMessage, this,
          &MainWindowController::onLogMessage);

  // ── Telegram Panel Controller ──
  TelegramPanelController::UiRefs telegramUiRefs;
  telegramUiRefs.userCountLabel = m_ui.userCountLabel;
  telegramUiRefs.entryPlateInput = m_ui.entryPlateInput;
  telegramUiRefs.btnSendEntry = m_ui.btnSendEntry;
  telegramUiRefs.exitPlateInput = m_ui.exitPlateInput;
  telegramUiRefs.feeInput = m_ui.feeInput;
  telegramUiRefs.btnSendExit = m_ui.btnSendExit;
  telegramUiRefs.userTable = m_ui.userTable;
  telegramUiRefs.chkShowPlateLogs = m_ui.chkShowPlateLogs;
  telegramUiRefs.logView = m_ui.logView;

  m_telegramPanelController =
      new TelegramPanelController(telegramUiRefs, m_telegramApi, this);

  // Users refresh: TelegramPanel + DbPanel sync
  connect(m_telegramPanelController, &TelegramPanelController::usersRefreshed,
          m_dbPanelController, &DbPanelController::refreshUserTable);
  if (m_telegramApi) {
    connect(m_telegramApi, &TelegramBotAPI::usersUpdated, m_dbPanelController,
            &DbPanelController::refreshUserTable);
  }

  // ── ROI Panel Controller ──
  RoiPanelController::UiRefs roiUiRefs;
  roiUiRefs.roiTargetCombo = m_ui.roiTargetCombo;
  roiUiRefs.roiNameEdit = m_ui.roiNameEdit;
  roiUiRefs.roiSelectorCombo = m_ui.roiSelectorCombo;
  roiUiRefs.logView = m_ui.logView;
  roiUiRefs.btnApplyRoi = m_ui.btnApplyRoi;
  roiUiRefs.btnFinishRoi = m_ui.btnFinishRoi;
  roiUiRefs.btnDeleteRoi = m_ui.btnDeleteRoi;
  roiUiRefs.videoWidgetPrimary = m_ui.videoWidgetPrimary;
  roiUiRefs.videoWidgetSecondary = m_ui.videoWidgetSecondary;

  RoiPanelController::Context roiCtx;
  roiCtx.channelAt = [this](int index) { return channelAt(index); };
  roiCtx.refreshZoneTable = [this]() { refreshZoneTableAllChannels(); };

  m_roiPanelController = new RoiPanelController(roiUiRefs, roiCtx, this);

  // ROI target change → update ReID panel active state
  connect(m_roiPanelController, &RoiPanelController::roiTargetChanged, this,
          [this](int target) {
            if (m_channels[0]) {
              m_channels[0]->setReidPanelActive(target == 0);
            }
            if (m_channels[1]) {
              m_channels[1]->setReidPanelActive(target == 1);
            }
          });
  connect(m_roiPanelController, &RoiPanelController::logMessage, this,
          &MainWindowController::onLogMessage);

  // ── Resize debounce ──
  m_resizeDebounceTimer = new QTimer(this);
  m_resizeDebounceTimer->setSingleShot(true);
  connect(m_resizeDebounceTimer, &QTimer::timeout, this,
          &MainWindowController::onVideoWidgetResizedSlot);

  if (m_ui.videoWidgetPrimary) {
    m_ui.videoWidgetPrimary->installEventFilter(this);
  }
  if (m_ui.videoWidgetSecondary) {
    m_ui.videoWidgetSecondary->installEventFilter(this);
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

  if (m_channels[0]) {
    m_channels[0]->setReidPanelActive(true);
  }
  if (m_channels[1]) {
    m_channels[1]->setReidPanelActive(false);
  }

  initRoiDbForChannels();
  m_roiPanelController->refreshSelector();
  updateChannelCardSelection();
  connectSignals();
}

void MainWindowController::startInitialCctv() { onChannelCardClicked(0); }

void MainWindowController::onSystemConfigChanged() {
  CameraChannelRuntime *primary = channelAt(0);
  if (!primary || primary->selectedCardIndex() != -1) {
    return;
  }
  onChannelCardClicked(0);
}

bool MainWindowController::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::Resize) {
    if (obj == m_ui.videoWidgetPrimary || obj == m_ui.videoWidgetSecondary) {
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
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->stop();
    }
  }
  if (m_rpiPanelController) {
    m_rpiPanelController->shutdown();
  }
  if (m_mediaCaptureController) {
    m_mediaCaptureController->shutdown();
  }

  const QString shutdownLog =
      QString("[Shutdown] camera/session stop finished in %1 ms")
          .arg(timer.elapsed());
  qDebug() << shutdownLog;
  if (m_ui.logView) {
    m_ui.logView->append(shutdownLog);
  }
}

void MainWindowController::connectSignals() {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->connectSignals();
    }
  }

  // ROI panel signals
  if (m_roiPanelController) {
    m_roiPanelController->connectSignals();
  }

  // ReID table click
  if (m_ui.reidTable) {
    connect(m_ui.reidTable, &QTableWidget::cellClicked, this,
            &MainWindowController::onReidTableCellClicked);
  }

  // FPS display toggle
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

  // Telegram panel signals
  if (m_telegramPanelController) {
    m_telegramPanelController->connectSignals();
  }

  // RPi panel signals
  if (m_rpiPanelController) {
    m_rpiPanelController->connectSignals();
  }

  // DB panel signals
  if (m_dbPanelController) {
    m_dbPanelController->connectSignals();
    m_dbPanelController->refreshAll();
  }

  // Media capture signals
  if (m_mediaCaptureController) {
    m_mediaCaptureController->connectSignals();
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
  const int primaryIndex =
      channelAt(0) ? channelAt(0)->selectedCardIndex() : -1;
  const int secondaryIndex =
      channelAt(1) ? channelAt(1)->selectedCardIndex() : -1;
  QStringList cameraKeys = Config::instance().cameraKeys();

  for (int i = 0; i < 4; ++i) {
    if (m_ui.channelCards[i]) {
      const bool isSelected = (i == primaryIndex || i == secondaryIndex);
      m_ui.channelCards[i]->setProperty("selected", isSelected);
      m_ui.channelCards[i]->style()->unpolish(m_ui.channelCards[i]);
      m_ui.channelCards[i]->style()->polish(m_ui.channelCards[i]);
    }
    if (m_ui.channelStatusDots[i]) {
      const bool hasSignal = i < cameraKeys.size();
      const bool isSelected = (i == primaryIndex || i == secondaryIndex);
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
          style = isSelected
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
    if (m_mediaCaptureController) {
      connect(source, &CameraSource::rawFrameReady, m_mediaCaptureController,
              &MediaCaptureController::onRawFrameReady);
    }
    connect(source, &CameraSource::thumbnailFrameReady, this,
            &MainWindowController::updateThumbnailForCard);
    connect(source, &CameraSource::statusChanged, this,
            [this](int, CameraSource::Status, const QString &) {
              updateChannelCardSelection();
            });
    connect(source, &CameraSource::logMessage, this,
            &MainWindowController::onLogMessage);
    m_cameraSources[static_cast<size_t>(i)] = source;
    source->start();
  }
}

void MainWindowController::onChannelCardClicked(int index) {
  if (index < 0 || index >= 4) {
    return;
  }

  CameraChannelRuntime *primary = channelAt(0);
  CameraChannelRuntime *secondary = channelAt(1);
  if (!primary || !secondary) {
    return;
  }

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

  auto deactivateChannel =
      [this, &clearThumbnail](CameraChannelRuntime *channel, int cardIndex) {
        channel->deactivate();
        updateChannelCardSelection();
        onLogMessage(QString("[Camera] Ch %1 해제").arg(cardIndex + 1));
        clearThumbnail(cardIndex);
        refreshZoneTableAllChannels();
        if (m_roiPanelController) {
          m_roiPanelController->refreshSelector();
        }
      };

  auto activateChannel = [this, index, isNoSignal, newSource](
                             CameraChannelRuntime *channel, int targetIndex) {
    if (isNoSignal) {
      channel->selectCardWithoutStream(index);
      updateChannelCardSelection();
      onLogMessage(QString("[Camera] Ch %1: 신호 없음").arg(index + 1));
      if (m_roiPanelController) {
        m_roiPanelController->refreshSelector();
      }
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
    int roiTarget =
        m_roiPanelController ? m_roiPanelController->roiTarget() : 0;
    if (targetIndex == roiTarget) {
      m_roiPanelController->refreshSelector();
    }
  };

  if (index == primary->selectedCardIndex()) {
    deactivateChannel(primary, index);
    return;
  }

  if (index == secondary->selectedCardIndex()) {
    deactivateChannel(secondary, index);
    return;
  }

  if (primary->selectedCardIndex() == -1) {
    activateChannel(primary, 0);
    return;
  }

  if (secondary->selectedCardIndex() == -1) {
    activateChannel(secondary, 1);
    return;
  }

  onLogMessage(QString("[Camera] 이미 두 개의 채널이 켜져 있습니다. 시청을 "
                       "원하는 채널을 끄고 다시 시도하세요."));
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
