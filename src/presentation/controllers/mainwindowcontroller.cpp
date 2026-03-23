#include "mainwindowcontroller.h"

#include "application/db/parking/parkinglogapplicationservice.h"
#include "application/db/user/useradminapplicationservice.h"
#include "application/db/zone/zonequeryapplicationservice.h"
#include "application/parking/parkingservice.h"
#include "infrastructure/camera/camerasource.h"
#include "infrastructure/persistence/databasecontext.h"
#include "infrastructure/persistence/mediarepository.h"
#include "presentation/controllers/camerasessioncontroller.h"
#include "presentation/controllers/channelruntimecontroller.h"
#include "presentation/controllers/cctvcontroller.h"
#include "presentation/controllers/dbpanelcontroller.h"
#include "presentation/controllers/hardwarecontroller.h"
#include "presentation/controllers/notificationcontroller.h"
#include "presentation/controllers/reidcontroller.h"
#include "presentation/controllers/recordpanelcontroller.h"
#include "presentation/controllers/recordingworkflowcontroller.h"
#include "presentation/controllers/telegrampanelcontroller.h"
#include "presentation/shell/mainwindow.h"
#include "presentation/widgets/controllerdialog.h"
#include "presentation/widgets/videowidget.h"
#include "infrastructure/telegram/telegrambotapi.h"
#include "presentation/widgets/camerachannelruntime.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>

MainWindowController::MainWindowController(const MainWindowUiRefs &uiRefs,
                                           QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
  TelegramPanelController::UiRefs telegramUiRefs;
  telegramUiRefs.userCountLabel = m_ui.userCountLabel;
  telegramUiRefs.entryPlateInput = m_ui.entryPlateInput;
  telegramUiRefs.btnSendEntry = m_ui.btnSendEntry;
  telegramUiRefs.exitPlateInput = m_ui.exitPlateInput;
  telegramUiRefs.feeInput = m_ui.feeInput;
  telegramUiRefs.btnSendExit = m_ui.btnSendExit;
  telegramUiRefs.userTable = m_ui.userTable;

  TelegramPanelController::Context telegramContext;
  telegramContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
  };
  telegramContext.refreshUserTable = [this]() {
    if (m_dbPanelController) {
      m_dbPanelController->refreshUserTable();
    }
  };
  telegramContext.updatePayment = [this](const QString &plate, int amount) {
    bool updated = false;
    const int sourceCount =
        m_cameraSessionController ? m_cameraSessionController->sourceCount() : 0;
    for (int i = 0; i < sourceCount; ++i) {
      ParkingService *service = parkingServiceForCardIndex(i);
      if (!service) {
        continue;
      }

      QString error;
      if (service->updatePayment(plate, amount, QStringLiteral("결제완료"),
                                 &error)) {
        updated = true;
      }
    }
    return updated;
  };
  telegramContext.refreshParkingLogs = [this]() {
    if (m_dbPanelController) {
      m_dbPanelController->onRefreshParkingLogs();
    }
  };
  m_telegramController =
      new TelegramPanelController(telegramUiRefs, telegramContext, this);

  CameraSessionController::UiRefs cameraSessionUiRefs;
  for (int i = 0; i < 4; ++i) {
    cameraSessionUiRefs.thumbnailLabels[i] = m_ui.thumbnailLabels[i];
  }
  CameraSessionController::Context cameraSessionContext;
  cameraSessionContext.telegramApi = [this]() {
    return m_telegramController ? m_telegramController->api() : nullptr;
  };
  m_cameraSessionController =
      new CameraSessionController(cameraSessionUiRefs, cameraSessionContext, this);
  connect(m_cameraSessionController, &CameraSessionController::rawFrameReady, this,
          &MainWindowController::onRawFrameReady);
  connect(m_cameraSessionController, &CameraSessionController::statusChanged, this,
          [this](int, int, const QString &) {
            if (m_cctvController) {
              m_cctvController->updateChannelCardSelection();
            }
          });
  connect(m_cameraSessionController, &CameraSessionController::logMessage, this,
          &MainWindowController::onLogMessage);

  m_notificationController =
      new NotificationController(m_ui.stackedWidget, this);

  const QString dbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/veda.db");
  DatabaseContext::init(dbPath);

  CctvController::UiRefs cctvUiRefs;
  for (int i = 0; i < 4; ++i) {
    cctvUiRefs.videoWidgets[i] = m_ui.videoWidgets[i];
    cctvUiRefs.channelCards[i] = m_ui.channelCards[i];
    cctvUiRefs.channelStatusDots[i] = m_ui.channelStatusDots[i];
    cctvUiRefs.channelNameLabels[i] = m_ui.channelNameLabels[i];
    cctvUiRefs.thumbnailLabels[i] = m_ui.thumbnailLabels[i];
  }
  cctvUiRefs.videoGridLayout = m_ui.videoGridLayout;
  cctvUiRefs.roiTargetCombo = m_ui.roiTargetCombo;
  cctvUiRefs.roiNameEdit = m_ui.roiNameEdit;
  cctvUiRefs.roiSelectorCombo = m_ui.roiSelectorCombo;
  cctvUiRefs.btnApplyRoi = m_ui.btnApplyRoi;
  cctvUiRefs.btnFinishRoi = m_ui.btnFinishRoi;
  cctvUiRefs.btnDeleteRoi = m_ui.btnDeleteRoi;

  CctvController::Context cctvContext;
  cctvContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
  };
  cctvContext.refreshZoneTable = [this]() { refreshZoneTableAllChannels(); };
  cctvContext.refreshParkingLogs = [this]() {
    if (m_dbPanelController) {
      m_dbPanelController->onRefreshParkingLogs();
    }
  };
  cctvContext.appendRoiStructuredLog = [this](const QJsonObject &roiData) {
    appendRoiStructuredLog(roiData);
  };
  cctvContext.notifyRoiCreated = [this](const QString &name) {
    if (m_notificationController) {
      m_notificationController->showRoiCreated(name);
    }
  };
  cctvContext.notifyRoiDeleted = [this](const QString &name) {
    if (m_notificationController) {
      m_notificationController->showRoiDeleted(name);
    }
  };
  cctvContext.channelAt = [this](int index) {
    return m_channelRuntimeController ? m_channelRuntimeController->channelAt(index)
                                      : nullptr;
  };
  cctvContext.channelForCardIndex = [this](int cardIndex) {
    return m_channelRuntimeController
               ? m_channelRuntimeController->channelForCardIndex(cardIndex)
               : nullptr;
  };
  cctvContext.sourceAt = [this](int cardIndex) { return sourceAt(cardIndex); };
  cctvContext.cardIndexForVideoWidget = [this](const VideoWidget *videoWidget) {
    return m_channelRuntimeController
               ? m_channelRuntimeController->cardIndexForVideoWidget(videoWidget)
               : -1;
  };
  m_cctvController = new CctvController(cctvUiRefs, cctvContext, this);
  m_cctvController->initializeViewState();

  ChannelRuntimeController::UiRefs channelRuntimeUiRefs;
  for (int i = 0; i < 4; ++i) {
    channelRuntimeUiRefs.videoWidgets[i] = m_ui.videoWidgets[i];
    channelRuntimeUiRefs.channelCards[i] = m_ui.channelCards[i];
    channelRuntimeUiRefs.channelNameLabels[i] = m_ui.channelNameLabels[i];
    channelRuntimeUiRefs.thumbnailLabels[i] = m_ui.thumbnailLabels[i];
  }
  channelRuntimeUiRefs.recordVideoWidget = m_ui.recordVideoWidget;
  channelRuntimeUiRefs.staleTimeoutInput = m_ui.staleTimeoutInput;
  channelRuntimeUiRefs.pruneTimeoutInput = m_ui.pruneTimeoutInput;
  channelRuntimeUiRefs.chkShowStaleObjects = m_ui.chkShowStaleObjects;
  channelRuntimeUiRefs.chkShowFps = m_ui.chkShowFps;
  channelRuntimeUiRefs.avgFpsLabel = m_ui.lblAvgFps;

  ChannelRuntimeController::Context channelRuntimeContext;
  channelRuntimeContext.handleMousePress = [this](QObject *obj) {
    return m_cctvController && m_cctvController->handleMousePress(obj);
  };
  channelRuntimeContext.onRecordPreviewResize = [this]() {
    if (m_recordingWorkflowController) {
      m_recordingWorkflowController->updateRecordPreviewSourceSize();
    }
  };
  m_channelRuntimeController =
      new ChannelRuntimeController(channelRuntimeUiRefs, channelRuntimeContext, this);
  connect(m_channelRuntimeController, &ChannelRuntimeController::zoneStateChanged,
          this, &MainWindowController::refreshZoneTableAllChannels);
  connect(m_channelRuntimeController, &ChannelRuntimeController::primaryVideoReady,
          this, &MainWindowController::primaryVideoReady);

  if (m_cameraSessionController) {
    m_cameraSessionController->start();
  }

  DbPanelController::UiRefs dbUiRefs;
  dbUiRefs.parkingLogTable = m_ui.parkingLogTable;
  dbUiRefs.plateSearchInput = m_ui.plateSearchInput;
  dbUiRefs.btnSearchPlate = m_ui.btnSearchPlate;
  dbUiRefs.btnRefreshLogs = m_ui.btnRefreshLogs;
  dbUiRefs.forcePlateInput = m_ui.forcePlateInput;
  dbUiRefs.forceObjectIdInput = m_ui.forceObjectIdInput;
  dbUiRefs.btnForcePlate = m_ui.btnForcePlate;
  dbUiRefs.editPlateInput = m_ui.editPlateInput;
  dbUiRefs.btnEditPlate = m_ui.btnEditPlate;
  dbUiRefs.userDbTable = m_ui.userDbTable;
  dbUiRefs.btnRefreshUsers = m_ui.btnRefreshUsers;
  dbUiRefs.btnAddUser = m_ui.btnAddUser;
  dbUiRefs.btnEditUser = m_ui.btnEditUser;
  dbUiRefs.btnDeleteUser = m_ui.btnDeleteUser;
  dbUiRefs.zoneTable = m_ui.zoneTable;
  dbUiRefs.btnRefreshZone = m_ui.btnRefreshZone;

  ParkingLogApplicationService::Context parkingLogServiceContext;
  parkingLogServiceContext.parkingServiceProvider = [this]() {
    const int targetIndex =
        m_cctvController ? m_cctvController->currentRoiTargetIndex() : 0;
    return parkingServiceForCardIndex(targetIndex);
  };
  parkingLogServiceContext.allParkingServicesProvider = [this]() {
    QVector<ParkingService *> services;
    const int sourceCount =
        m_cameraSessionController ? m_cameraSessionController->sourceCount() : 0;
    for (int i = 0; i < sourceCount; ++i) {
      ParkingService *service = parkingServiceForCardIndex(i);
      if (service) {
        services.append(service);
      }
    }
    return services;
  };
  parkingLogServiceContext.parkingServiceForCameraKeyProvider =
      [this](const QString &cameraKey) {
    const int sourceCount =
        m_cameraSessionController ? m_cameraSessionController->sourceCount() : 0;
    for (int i = 0; i < sourceCount; ++i) {
      CameraSource *source = sourceAt(i);
      if (!source || source->cameraKey() != cameraKey) {
        continue;
      }
      return source->parkingService();
    }
    return static_cast<ParkingService *>(nullptr);
  };

  UserAdminApplicationService::Context userAdminServiceContext;
  userAdminServiceContext.userDeleted = [this](const QString &chatId) {
    if (m_telegramController && m_telegramController->api()) {
      m_telegramController->api()->removeUser(chatId);
    }
  };

  ZoneQueryApplicationService::Context zoneQueryServiceContext;
  zoneQueryServiceContext.allZoneRecordsProvider = [this]() {
    QVector<QJsonObject> allRecords;
    const int sourceCount =
        m_cameraSessionController ? m_cameraSessionController->sourceCount() : 0;
    for (int i = 0; i < sourceCount; ++i) {
      CameraSource *source = sourceAt(i);
      if (!source) {
        continue;
      }

      const QVector<QJsonObject> &records = source->roiRecords();
      allRecords.reserve(allRecords.size() + records.size());
      for (const QJsonObject &record : records) {
        allRecords.append(record);
      }
    }
    return allRecords;
  };

  auto *parkingLogService =
      new ParkingLogApplicationService(parkingLogServiceContext, this);
  auto *userAdminService =
      new UserAdminApplicationService(userAdminServiceContext, this);
  auto *zoneQueryService =
      new ZoneQueryApplicationService(zoneQueryServiceContext, this);

  DbPanelController::Context dbContext;
  dbContext.parkingLogService = parkingLogService;
  dbContext.userAdminService = userAdminService;
  dbContext.zoneQueryService = zoneQueryService;
  dbContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
  };
  m_dbPanelController = new DbPanelController(dbUiRefs, dbContext, this);
  const int sourceCount =
      m_cameraSessionController ? m_cameraSessionController->sourceCount() : 0;
  for (int i = 0; i < sourceCount; ++i) {
    ParkingService *service = parkingServiceForCardIndex(i);
    if (!service) {
      continue;
    }
    connect(service, &ParkingService::vehicleEntered, m_dbPanelController,
            &DbPanelController::onRefreshParkingLogs);
    connect(service, &ParkingService::vehicleDeparted, m_dbPanelController,
            &DbPanelController::onRefreshParkingLogs);
    connect(service, &ParkingService::vehicleEntered, this,
            [this, service](int roiIndex, const QString &) {
              if (m_notificationController) {
                m_notificationController->showVehicleEntered(
                    service->zoneNameForIndex(roiIndex));
              }
            });
    connect(service, &ParkingService::vehicleDeparted, this,
            [this, service](int roiIndex, const QString &) {
              if (m_notificationController) {
                m_notificationController->showVehicleDeparted(
                    service->zoneNameForIndex(roiIndex));
              }
            });
  }
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

  RecordingWorkflowController::UiRefs recordingUiRefs;
  recordingUiRefs.btnCaptureManual = m_ui.btnCaptureManual;
  recordingUiRefs.btnCaptureRecordTab = m_ui.btnCaptureRecordTab;
  recordingUiRefs.btnRecordManual = m_ui.btnRecordManual;
  recordingUiRefs.btnRecordRecordTab = m_ui.btnRecordRecordTab;
  recordingUiRefs.btnApplyContinuousSetting = m_ui.btnApplyContinuousSetting;
  recordingUiRefs.cmbManualCamera = m_ui.cmbManualCamera;
  recordingUiRefs.spinRecordRetention = m_ui.spinRecordRetention;
  recordingUiRefs.recordVideoWidget = m_ui.recordVideoWidget;

  RecordingWorkflowController::Context recordingContext;
  recordingContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
  };
  recordingContext.selectedCctvChannelIndex = [this]() {
    return m_cctvController ? m_cctvController->selectedChannelIndex() : 0;
  };
  recordingContext.sourceAt = [this](int cardIndex) {
    return sourceAt(cardIndex);
  };
  m_recordingWorkflowController = new RecordingWorkflowController(
      recordingUiRefs, recordingContext, m_mediaRepo, m_recordPanelController,
      this);

  ReidController::UiRefs reidUiRefs;
  reidUiRefs.reidTable = m_ui.reidTable;
  reidUiRefs.staleTimeoutInput = m_ui.staleTimeoutInput;
  reidUiRefs.pruneTimeoutInput = m_ui.pruneTimeoutInput;
  reidUiRefs.chkShowStaleObjects = m_ui.chkShowStaleObjects;
  reidUiRefs.forcePlateInput = m_ui.forcePlateInput;
  reidUiRefs.forceObjectIdInput = m_ui.forceObjectIdInput;
  reidUiRefs.btnForcePlate = m_ui.btnForcePlate;

  ReidController::Context reidContext;
  reidContext.sourceAt = [this](int cardIndex) { return sourceAt(cardIndex); };
  reidContext.sourceCount = [this]() {
    return m_cameraSessionController ? m_cameraSessionController->sourceCount()
                                     : 0;
  };
  m_reidController = new ReidController(reidUiRefs, reidContext, this);

  HardwareController::UiRefs hardwareUiRefs;
  hardwareUiRefs.btnRecordManual = m_ui.btnRecordManual;
  hardwareUiRefs.stackedWidget = m_ui.stackedWidget;
  hardwareUiRefs.dbSubTabs = m_ui.dbSubTabs;

  HardwareController::Context hardwareContext;
  hardwareContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
  };
  hardwareContext.selectedChannelCount = [this]() {
    return m_cctvController ? m_cctvController->selectedChannelCount() : 0;
  };
  hardwareContext.primarySelectedVideoWidget = [this]() -> VideoWidget * {
    const int cardIndex =
        m_cctvController ? m_cctvController->primarySelectedChannelIndex() : 0;
    auto *channel = m_channelRuntimeController
                        ? m_channelRuntimeController->channelForCardIndex(cardIndex)
                        : nullptr;
    if (!channel) {
      channel = m_channelRuntimeController
                    ? m_channelRuntimeController->channelAt(0)
                    : nullptr;
    }
    return channel ? channel->videoWidget() : nullptr;
  };
  hardwareContext.resetAllChannelZoom = [this]() {
    if (m_channelRuntimeController) {
      m_channelRuntimeController->resetAllChannelZoom();
    }
  };
  hardwareContext.selectSingleChannel = [this](int channelIndex) {
    if (m_cctvController) {
      m_cctvController->selectSingleChannel(channelIndex);
    }
  };
  hardwareContext.captureManual = [this]() {
    if (m_recordingWorkflowController) {
      m_recordingWorkflowController->triggerManualCapture();
    }
  };
  hardwareContext.setManualRecording = [this](bool recording) {
    setManualRecordingFromHardware(recording);
  };
  hardwareContext.isManualRecording = [this]() {
    return m_recordingWorkflowController &&
           m_recordingWorkflowController->isManualRecording();
  };
  hardwareContext.dbPageIndex = MainWindow::kDbPageIndex;
  m_hardwareController =
      new HardwareController(hardwareUiRefs, hardwareContext, this);

  if (m_channelRuntimeController) {
    m_channelRuntimeController->setReidPanelActiveForAll(false);
  }

  initRoiDbForChannels();
  if (m_cctvController) {
    m_cctvController->refreshRoiSelectorForTarget();
    m_cctvController->updateChannelCardSelection();
  }
  if (m_reidController) {
    m_reidController->refresh(true);
  }
  connectSignals();
  if (m_hardwareController) {
    m_hardwareController->connectSignals();
  }
}

void MainWindowController::startInitialCctv() {
  if (m_cctvController) {
    m_cctvController->startInitialCctv();
  }
}

void MainWindowController::onSystemConfigChanged() {
  if (m_cctvController) {
    m_cctvController->onSystemConfigChanged();
  }
}

void MainWindowController::shutdown() {
  QElapsedTimer timer;
  timer.start();

  const QString summary = m_logDeduplicator.flushPending();
  if (!summary.isEmpty()) {
    qDebug().noquote() << summary;
  }
  if (m_channelRuntimeController) {
    m_channelRuntimeController->shutdown();
  }
  if (m_recordingWorkflowController) {
    m_recordingWorkflowController->shutdown();
  }
  if (m_reidController) {
    m_reidController->shutdown();
  }
  if (m_hardwareController) {
    m_hardwareController->shutdown();
  }
  if (m_cameraSessionController) {
    m_cameraSessionController->shutdown();
  }

  const QString shutdownLog =
      QString("[Shutdown] camera/session stop finished in %1 ms")
          .arg(timer.elapsed());
  qDebug().noquote() << shutdownLog;
}

void MainWindowController::connectSignals() {
  if (m_channelRuntimeController) {
    m_channelRuntimeController->connectSignals();
  }
  if (m_cctvController) {
    m_cctvController->connectSignals();
  }
  if (m_reidController) {
    m_reidController->connectSignals();
  }

  if (m_telegramController) {
    m_telegramController->connectSignals();
  }

  if (m_dbPanelController) {
    m_dbPanelController->connectSignals();
    m_dbPanelController->refreshAll();
  }

  if (m_recordingWorkflowController) {
    m_recordingWorkflowController->connectSignals();
  }
}

void MainWindowController::setManualRecordingFromHardware(bool recording) {
  if (m_recordingWorkflowController) {
    m_recordingWorkflowController->setManualRecordingFromHardware(recording);
  }
}

void MainWindowController::connectControllerDialog(ControllerDialog *dialog) {
  if (m_hardwareController) {
    m_hardwareController->connectControllerDialog(dialog);
  }
}

void MainWindowController::refreshZoneTableAllChannels() {
  if (m_dbPanelController) {
    m_dbPanelController->refreshZoneTable();
  }
}

void MainWindowController::initRoiDb() { initRoiDbForChannels(); }

void MainWindowController::initRoiDbForChannels() {
  const int sourceCount =
      m_cameraSessionController ? m_cameraSessionController->sourceCount() : 0;
  for (int i = 0; i < sourceCount; ++i) {
    CameraSource *source = sourceAt(i);
    if (source) {
      source->reloadRoi(false);
    }
  }
}

void MainWindowController::appendRoiStructuredLog(const QJsonObject &roiData) {
  const QString line =
      QString::fromUtf8(QJsonDocument(roiData).toJson(QJsonDocument::Compact));
  qDebug().noquote() << QString("[ROI][Structured] %1").arg(line);
}

void MainWindowController::updateObjectFilter(
    const QSet<QString> &disabledTypes) {
  if (m_cameraSessionController) {
    m_cameraSessionController->updateObjectFilter(disabledTypes);
  }
}

void MainWindowController::onLogMessage(const QString &msg) {
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const LogDeduplicator::IngestResult ingestResult =
      m_logDeduplicator.ingest(msg, nowMs);

  if (!ingestResult.flushSummary.isEmpty()) {
    qDebug().noquote() << ingestResult.flushSummary;
  }
  if (ingestResult.suppressed) {
    return;
  }

  qDebug().noquote() << msg;
}

void MainWindowController::onRawFrameReady(int cardIndex,
                                           SharedVideoFrame frame) {
  if (cardIndex < 0 || cardIndex >= 4 || !frame.isValid()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - frame.timestampMs) > 100) {
    return;
  }

  if (m_recordingWorkflowController) {
    m_recordingWorkflowController->ingestRawFrame(cardIndex, frame);
  }

  if (m_reidController) {
    m_reidController->refresh(false);
  }
}

CameraSource *MainWindowController::sourceAt(int cardIndex) const {
  return m_cameraSessionController ? m_cameraSessionController->sourceAt(cardIndex)
                                   : nullptr;
}

ParkingService *
MainWindowController::parkingServiceForCardIndex(int cardIndex) const {
  return m_cameraSessionController
             ? m_cameraSessionController->parkingServiceForCardIndex(cardIndex)
             : nullptr;
}
