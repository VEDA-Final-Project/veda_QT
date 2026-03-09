#include "mainwindowcontroller.h"

#include "camera/camerasource.h"
#include "camerachannelruntime.h"
#include "config/config.h"
#include "database/databasecontext.h"
#include "dbpanelcontroller.h"
#include "parking/parkingservice.h"
#include "roi/roiservice.h"
#include "rpipanelcontroller.h"
#include "ui/video/videowidget.h"
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
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QTextEdit>
#include <QSpinBox>

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

  m_channels[0] = new CameraChannelRuntime(CameraChannelRuntime::Slot::Primary,
                                           QStringLiteral("A"),
                                           m_ui.videoWidgetPrimary,
                                           channelUiRefs, this);
  m_channels[1] = new CameraChannelRuntime(CameraChannelRuntime::Slot::Secondary,
                                           QStringLiteral("B"),
                                           m_ui.videoWidgetSecondary,
                                           channelUiRefs, this);

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
  dbContext.logMessage = [this](const QString &message) { onLogMessage(message); };
  dbContext.userDeleted = [this](const QString &chatId) {
    if (m_telegramApi) {
      m_telegramApi->removeUser(chatId);
    }
  };
  m_dbPanelController = new DbPanelController(dbUiRefs, dbContext, this);

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
  refreshRoiSelectorForTarget();
  updateChannelCardSelection();
  connectSignals();
}

void MainWindowController::startInitialCctv() {
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
  if (m_ui.videoWidgetPrimary) {
    connect(m_ui.videoWidgetPrimary, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(m_ui.videoWidgetPrimary, &VideoWidget::roiPolygonChanged, this,
            &MainWindowController::onRoiPolygonChanged);
  }
  if (m_ui.videoWidgetSecondary) {
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiPolygonChanged, this,
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
  const int primaryIndex = channelAt(0) ? channelAt(0)->selectedCardIndex() : -1;
  const int secondaryIndex = channelAt(1) ? channelAt(1)->selectedCardIndex() : -1;
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
                      ? QStringLiteral(
                            "background: #00e676; border-radius: 5px; border: none;")
                      : QStringLiteral(
                            "background: #10b981; border-radius: 5px; border: none;");
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

void MainWindowController::onRoiTargetChanged(int index) {
  m_roiTarget = (index == 1) ? RoiTarget::Secondary : RoiTarget::Primary;
  if (m_channels[0]) {
    m_channels[0]->setReidPanelActive(m_roiTarget == RoiTarget::Primary);
  }
  if (m_channels[1]) {
    m_channels[1]->setReidPanelActive(m_roiTarget == RoiTarget::Secondary);
  }
  refreshRoiSelectorForTarget();
  onLogMessage(
      QString("[ROI] 편집 대상 변경: %1")
          .arg(m_roiTarget == RoiTarget::Primary ? "카메라 A" : "카메라 B"));
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
    m_ui.thumbnailLabels[cardIndex]->setText(hasSignal ? QStringLiteral("STANDBY")
                                                       : QStringLiteral("NO SIGNAL"));
  };

  auto deactivateChannel = [this, &clearThumbnail](CameraChannelRuntime *channel,
                                                   int cardIndex) {
    channel->deactivate();
    updateChannelCardSelection();
    onLogMessage(QString("[Camera] Ch %1 해제").arg(cardIndex + 1));
    clearThumbnail(cardIndex);
    refreshZoneTableAllChannels();
    refreshRoiSelectorForTarget();
  };

  auto activateChannel = [this, index, isNoSignal, newSource](
                             CameraChannelRuntime *channel, int targetIndex) {
    if (isNoSignal) {
      channel->selectCardWithoutStream(index);
      updateChannelCardSelection();
      onLogMessage(QString("[Camera] Ch %1: 신호 없음").arg(index + 1));
      refreshRoiSelectorForTarget();
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
    if (targetIndex == static_cast<int>(m_roiTarget)) {
      refreshRoiSelectorForTarget();
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
        delete m_ui.eventListWidget->takeItem(m_ui.eventListWidget->count() - 1);
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
          .arg(m_roiTarget == RoiTarget::Primary ? "카메라 A" : "카메라 B"));
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
  CameraChannelRuntime *targetChannel = channelAt(static_cast<int>(m_roiTarget));
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  RoiService *targetService = roiServiceForTarget(m_roiTarget);
  if (!m_ui.roiSelectorCombo || !targetChannel || !targetWidget || !targetService ||
      !m_ui.logView) {
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
  const QString channel = (sourceWidget == m_ui.videoWidgetSecondary)
                              ? QStringLiteral("B")
                              : QStringLiteral("A");
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
  if (sourceWidget == m_ui.videoWidgetSecondary) {
    target = RoiTarget::Secondary;
  } else if (sourceWidget == m_ui.videoWidgetPrimary) {
    target = RoiTarget::Primary;
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

VideoWidget *MainWindowController::videoWidgetForTarget(RoiTarget target) const {
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

ParkingService *MainWindowController::parkingServiceForTarget(RoiTarget target) {
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
