#include "mainwindowcontroller.h"

#include "camera/camerasource.h"
#include "config/config.h"
#include "database/databasecontext.h"
#include "database/mediarepository.h"
#include "parking/parkingservice.h"
#include "presentation/controllers/cctvcontroller.h"
#include "presentation/controllers/dbpanelcontroller.h"
#include "presentation/controllers/hardwarecontroller.h"
#include "presentation/controllers/recordpanelcontroller.h"
#include "presentation/controllers/recordingworkflowcontroller.h"
#include "presentation/controllers/telegrampanelcontroller.h"
#include "presentation/shell/mainwindow.h"
#include "presentation/widgets/controllerdialog.h"
#include "telegram/telegrambotapi.h"
#include "ui/windows/camerachannelruntime.h"
#include "presentation/widgets/videowidget.h"
#include <QCheckBox>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QJsonDocument>
#include <QPixmap>
#include <QPointer>
#include <QSpinBox>
#include <QTableWidget> // User Table
#include <QTableWidgetItem>
#include <QTimer>
#include <QtGlobal>
#include <algorithm>

namespace {
constexpr int kCameraStartStaggerMs = 0;
constexpr qint64 kReidRefreshIntervalMs = 300;
}

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
    for (CameraSource *source : m_cameraSources) {
      ParkingService *service = source ? source->parkingService() : nullptr;
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

  CameraChannelRuntime::SharedUiRefs channelUiRefs;
  channelUiRefs.reidTable = nullptr;
  channelUiRefs.staleTimeoutInput = m_ui.staleTimeoutInput;
  channelUiRefs.pruneTimeoutInput = m_ui.pruneTimeoutInput;
  channelUiRefs.chkShowStaleObjects = m_ui.chkShowStaleObjects;
  channelUiRefs.avgFpsLabel = m_ui.lblAvgFps;

  m_channels[0] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch1, m_ui.videoWidgets[0], channelUiRefs,
      this);
  m_channels[1] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch2, m_ui.videoWidgets[1], channelUiRefs,
      this);
  m_channels[2] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch3, m_ui.videoWidgets[2], channelUiRefs,
      this);
  m_channels[3] = new CameraChannelRuntime(
      CameraChannelRuntime::Slot::Ch4, m_ui.videoWidgets[3], channelUiRefs,
      this);

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
  cctvContext.channelAt = [this](int index) { return channelAt(index); };
  cctvContext.channelForCardIndex = [this](int cardIndex) {
    return channelForCardIndex(cardIndex);
  };
  cctvContext.sourceAt = [this](int cardIndex) { return sourceAt(cardIndex); };
  cctvContext.cardIndexForVideoWidget = [this](const VideoWidget *videoWidget) {
    return cardIndexForVideoWidget(videoWidget);
  };
  m_cctvController = new CctvController(cctvUiRefs, cctvContext, this);
  m_cctvController->initializeViewState();

  startCameraSources();

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

  DbPanelController::Context dbContext;
  dbContext.parkingServiceProvider = [this]() {
    const int targetIndex =
        m_cctvController ? m_cctvController->currentRoiTargetIndex() : 0;
    return parkingServiceForCardIndex(targetIndex);
  };
  dbContext.allParkingServicesProvider = [this]() {
    QVector<ParkingService *> services;
    for (CameraSource *source : m_cameraSources) {
      ParkingService *service = source ? source->parkingService() : nullptr;
      if (service) {
        services.append(service);
      }
    }
    return services;
  };
  dbContext.parkingServiceForCameraKeyProvider = [this](const QString &cameraKey) {
    for (CameraSource *source : m_cameraSources) {
      if (!source || source->cameraKey() != cameraKey) {
        continue;
      }
      return source->parkingService();
    }
    return static_cast<ParkingService *>(nullptr);
  };
  dbContext.allZoneRecordsProvider = [this]() {
    QVector<QJsonObject> allRecords;
    for (CameraSource *source : m_cameraSources) {
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
  dbContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
  };
  dbContext.userDeleted = [this](const QString &chatId) {
    if (m_telegramController && m_telegramController->api()) {
      m_telegramController->api()->removeUser(chatId);
    }
  };
  m_dbPanelController = new DbPanelController(dbUiRefs, dbContext, this);
  for (CameraSource *source : m_cameraSources) {
    ParkingService *service = source ? source->parkingService() : nullptr;
    if (!service) {
      continue;
    }
    connect(service, &ParkingService::vehicleEntered, m_dbPanelController,
            &DbPanelController::onRefreshParkingLogs);
    connect(service, &ParkingService::vehicleDeparted, m_dbPanelController,
            &DbPanelController::onRefreshParkingLogs);
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
    CameraChannelRuntime *channel = channelForCardIndex(cardIndex);
    if (!channel) {
      channel = channelAt(0);
    }
    return channel ? channel->videoWidget() : nullptr;
  };
  hardwareContext.resetAllChannelZoom = [this]() {
    for (int i = 0; i < 4; ++i) {
      CameraChannelRuntime *channel = channelAt(i);
      if (channel && channel->videoWidget()) {
        channel->videoWidget()->setZoom(1.0);
      }
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
      m_channels[i]->setReidPanelActive(false); // 개별 갱신 중지, 컨트롤러에서 통합 갱신
    }
  }

  m_reidTimer = new QTimer(this);
  m_reidTimer->setInterval(300);
  connect(m_reidTimer, &QTimer::timeout, this,
          &MainWindowController::onRefreshReidTableAllChannels);
  m_reidTimer->start();

  initRoiDbForChannels();
  if (m_cctvController) {
    m_cctvController->refreshRoiSelectorForTarget();
    m_cctvController->updateChannelCardSelection();
  }
  refreshReidTableAllChannels(true);
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
    if (m_cctvController && m_cctvController->handleMousePress(obj)) {
      return true;
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
  if (m_recordingWorkflowController) {
    m_recordingWorkflowController->updateRecordPreviewSourceSize();
  }
}

void MainWindowController::shutdown() {
  QElapsedTimer timer;
  timer.start();

  const QString summary = m_logDeduplicator.flushPending();
  if (!summary.isEmpty()) {
    qDebug().noquote() << summary;
  }

  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->shutdown();
    }
  }
  if (m_recordingWorkflowController) {
    m_recordingWorkflowController->shutdown();
  }
  if (m_hardwareController) {
    m_hardwareController->shutdown();
  }
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->stop();
    }
  }

  const QString shutdownLog =
      QString("[Shutdown] camera/session stop finished in %1 ms")
          .arg(timer.elapsed());
  qDebug().noquote() << shutdownLog;
  if (m_reidTimer) {
    m_reidTimer->stop();
  }
}

void MainWindowController::connectSignals() {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->connectSignals();
    }
  }
  if (m_cctvController) {
    m_cctvController->connectSignals();
  }
  if (m_ui.reidTable) {
    connect(m_ui.reidTable, &QTableWidget::cellClicked, this,
            &MainWindowController::onReidTableCellClicked);
  }
  if (m_ui.staleTimeoutInput) {
    connect(m_ui.staleTimeoutInput,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { refreshReidTableAllChannels(true); });
  }
  if (m_ui.pruneTimeoutInput) {
    connect(m_ui.pruneTimeoutInput,
            QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { refreshReidTableAllChannels(true); });
  }
  if (m_ui.chkShowStaleObjects) {
    connect(m_ui.chkShowStaleObjects, &QCheckBox::toggled, this,
            [this](bool) { refreshReidTableAllChannels(true); });
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

int MainWindowController::cardIndexForVideoWidget(
    const VideoWidget *videoWidget) const {
  if (!videoWidget) {
    return -1;
  }
  for (const CameraChannelRuntime *channel : m_channels) {
    if (channel && channel->videoWidget() == videoWidget) {
      return channel->selectedCardIndex();
    }
  }
  return -1;
}

void MainWindowController::refreshZoneTableAllChannels() {
  if (m_dbPanelController) {
    m_dbPanelController->refreshZoneTable();
  }
}

void MainWindowController::refreshReidTableAllChannels(bool force) {
  if (!m_ui.reidTable) {
    return;
  }

  if (!force && m_reidRefreshTimer.isValid() &&
      m_reidRefreshTimer.elapsed() < kReidRefreshIntervalMs) {
    return;
  }
  m_reidRefreshTimer.restart();

  QString selectedCameraKey;
  int selectedObjectId = -1;
  if (const int currentRow = m_ui.reidTable->currentRow(); currentRow >= 0) {
    if (QTableWidgetItem *objectIdItem = m_ui.reidTable->item(currentRow, 2)) {
      selectedObjectId = objectIdItem->text().toInt();
      selectedCameraKey = objectIdItem->data(Qt::UserRole).toString();
    } else if (QTableWidgetItem *idItem = m_ui.reidTable->item(currentRow, 1)) {
      selectedObjectId = idItem->data(Qt::UserRole + 1).toInt();
      selectedCameraKey = idItem->data(Qt::UserRole).toString();
    }
  }

  struct AggregatedVehicleRow {
    int cardIndex = -1;
    QString cameraKey;
    VehicleState state;
  };

  QVector<AggregatedVehicleRow> rows;
  const int staleMs =
      m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;
  const bool showStaleObjects =
      !m_ui.chkShowStaleObjects || m_ui.chkShowStaleObjects->isChecked();
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  for (int i = 0; i < static_cast<int>(m_cameraSources.size()); ++i) {
    CameraSource *source = sourceAt(i);
    if (!source) {
      continue;
    }

    const QList<VehicleState> activeVehicles = source->activeVehicles();
    for (const VehicleState &vehicle : activeVehicles) {
      if (vehicle.objectId < 0) {
        continue;
      }
      if (!isVehicleType(vehicle.type)) {
        continue;
      }
      if (vehicle.reidId.isEmpty() || vehicle.reidId == QStringLiteral("V---")) {
        continue;
      }

      const bool isStale = (nowMs - vehicle.lastSeenMs) > staleMs;
      if (isStale && !showStaleObjects) {
        continue;
      }

      rows.append({i, source->cameraKey(), vehicle});
    }
  }

  std::sort(rows.begin(), rows.end(),
            [](const AggregatedVehicleRow &a, const AggregatedVehicleRow &b) {
              if (a.cardIndex != b.cardIndex) {
                return a.cardIndex < b.cardIndex;
              }
              return a.state.objectId < b.state.objectId;
            });

  m_ui.reidTable->setUpdatesEnabled(false);
  const QSignalBlocker blocker(m_ui.reidTable);
  m_ui.reidTable->setRowCount(static_cast<int>(rows.size()));

  int rowToRestore = -1;
  for (int i = 0; i < rows.size(); ++i) {
    const AggregatedVehicleRow &rowData = rows[i];
    const int row = i;

    const bool isStale = (nowMs - rowData.state.lastSeenMs) > staleMs;
    const QColor textColor =
        isStale ? QColor(QStringLiteral("#94A3B8"))
                : QColor(QStringLiteral("#F8FAFC"));
    const QColor rowBackground = [cardIndex = rowData.cardIndex]() {
      switch (cardIndex) {
      case 0:
        return QColor(QStringLiteral("#17324A"));
      case 1:
        return QColor(QStringLiteral("#402636"));
      case 2:
        return QColor(QStringLiteral("#183D35"));
      case 3:
        return QColor(QStringLiteral("#433A1E"));
      default:
        return QColor(QStringLiteral("#1E293B"));
      }
    }();

    auto *channelItem =
        new QTableWidgetItem(QStringLiteral("Ch%1").arg(rowData.cardIndex + 1));
    channelItem->setForeground(textColor);
    channelItem->setBackground(rowBackground);
    channelItem->setData(Qt::UserRole, rowData.cameraKey);
    m_ui.reidTable->setItem(row, 0, channelItem);

    const QString displayId =
        (rowData.state.reidId.isEmpty() ||
         rowData.state.reidId == QStringLiteral("V---"))
            ? QString("V%1").arg(rowData.state.objectId)
            : rowData.state.reidId;
    auto *idItem = new QTableWidgetItem(displayId);
    idItem->setForeground(textColor);
    idItem->setBackground(rowBackground);
    idItem->setData(Qt::UserRole, rowData.cameraKey);
    idItem->setData(Qt::UserRole + 1, rowData.state.objectId);
    idItem->setToolTip(QString("Tracker ID: %1").arg(rowData.state.objectId));
    m_ui.reidTable->setItem(row, 1, idItem);

    auto *objectIdItem =
        new QTableWidgetItem(QString::number(rowData.state.objectId));
    objectIdItem->setForeground(textColor);
    objectIdItem->setBackground(rowBackground);
    objectIdItem->setData(Qt::UserRole, rowData.cameraKey);
    m_ui.reidTable->setItem(row, 2, objectIdItem);

    auto *plateItem = new QTableWidgetItem(rowData.state.plateNumber);
    plateItem->setForeground(textColor);
    plateItem->setBackground(rowBackground);
    plateItem->setData(Qt::UserRole, rowData.cameraKey);
    m_ui.reidTable->setItem(row, 3, plateItem);

    if (rowData.state.objectId == selectedObjectId &&
        rowData.cameraKey == selectedCameraKey) {
      rowToRestore = row;
    }
  }

  if (rowToRestore >= 0) {
    m_ui.reidTable->setCurrentCell(rowToRestore, 1);
    m_ui.reidTable->selectRow(rowToRestore);
  } else {
    m_ui.reidTable->clearSelection();
    if (m_ui.btnForcePlate) {
      m_ui.btnForcePlate->setProperty("cameraKey", QString());
    }
  }

  m_ui.reidTable->setUpdatesEnabled(true);
}

void MainWindowController::initRoiDb() { initRoiDbForChannels(); }

void MainWindowController::initRoiDbForChannels() {
  for (CameraSource *source : m_cameraSources) {
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
    source->initialize(m_telegramController ? m_telegramController->api()
                                            : nullptr);
    connect(source, &CameraSource::rawFrameReady, this,
            &MainWindowController::onRawFrameReady);
    connect(source, &CameraSource::thumbnailFrameReady, this,
            &MainWindowController::updateThumbnailForCard);
    connect(source, &CameraSource::statusChanged, this,
            [this](int, CameraSource::Status, const QString &) {
              if (m_cctvController) {
                m_cctvController->updateChannelCardSelection();
              }
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

void MainWindowController::updateObjectFilter(
    const QSet<QString> &disabledTypes) {
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->updateObjectFilter(disabledTypes);
    }
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

  refreshReidTableAllChannels(false);
}
void MainWindowController::onReidTableCellClicked(int row, int column) {
  Q_UNUSED(column);
  if (!m_ui.reidTable) {
    return;
  }

  QTableWidgetItem *idItem = m_ui.reidTable->item(row, 1);
  QTableWidgetItem *objectIdItem = m_ui.reidTable->item(row, 2);
  QTableWidgetItem *plateItem = m_ui.reidTable->item(row, 3);

  if (m_ui.forceObjectIdInput) {
    const int objectId =
        objectIdItem ? objectIdItem->text().toInt()
                     : (idItem ? idItem->data(Qt::UserRole + 1).toInt() : 0);
    m_ui.forceObjectIdInput->setValue(objectId);
  }

  if (plateItem && m_ui.forcePlateInput) {
    m_ui.forcePlateInput->setText(plateItem->text());
  }

  if (m_ui.btnForcePlate && idItem) {
    m_ui.btnForcePlate->setProperty("cameraKey",
                                    idItem->data(Qt::UserRole).toString());
  }

}

CameraChannelRuntime *MainWindowController::channelAt(int index) const {
  if (index < 0 || index >= static_cast<int>(m_channels.size())) {
    return nullptr;
  }
  return m_channels[static_cast<size_t>(index)];
}

CameraChannelRuntime *MainWindowController::channelForCardIndex(
    int cardIndex) const {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel && channel->selectedCardIndex() == cardIndex) {
      return channel;
    }
  }
  return nullptr;
}

CameraSource *MainWindowController::sourceAt(int cardIndex) const {
  if (cardIndex < 0 || cardIndex >= static_cast<int>(m_cameraSources.size())) {
    return nullptr;
  }
  return m_cameraSources[static_cast<size_t>(cardIndex)];
}

ParkingService *MainWindowController::parkingServiceForCardIndex(int cardIndex) {
  CameraSource *source = sourceAt(cardIndex);
  return source ? source->parkingService() : nullptr;
}

void MainWindowController::updateThumbnailForCard(int cardIndex,
                                                  SharedVideoFrame frame) {
  if (cardIndex < 0 || cardIndex >= 4 || !frame.isValid() ||
      !m_ui.thumbnailLabels[cardIndex]) {
    return;
  }

  if (m_renderTimerThumbs[cardIndex].isValid() &&
      m_renderTimerThumbs[cardIndex].elapsed() < 200) {
    return;
  }
  m_renderTimerThumbs[cardIndex].restart();

  const QSize targetSize = m_ui.thumbnailLabels[cardIndex]->size();
  ThumbnailCache &cache = m_thumbnailCaches[cardIndex];
  const cv::Mat *frameIdentity = frame.mat.data();
  if (cache.frameIdentity != frameIdentity || cache.labelSize != targetSize ||
      cache.pixmap.isNull()) {
    QImage image(frame.mat->data, frame.mat->cols, frame.mat->rows,
                 frame.mat->step, QImage::Format_BGR888);
    const QImage scaledImg =
        image.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    cache.frameIdentity = frameIdentity;
    cache.labelSize = targetSize;
    cache.pixmap = QPixmap::fromImage(scaledImg);
  }
  m_ui.thumbnailLabels[cardIndex]->setText(QString());
  QMetaObject::invokeMethod(m_ui.thumbnailLabels[cardIndex], "setPixmap",
                            Qt::QueuedConnection,
                            Q_ARG(QPixmap, cache.pixmap));
}

void MainWindowController::onRefreshReidTableAllChannels() {
  refreshReidTableAllChannels();
}
