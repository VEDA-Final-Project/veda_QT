#include "mainwindowcontroller.h"

#include "config/config.h"
#include "database/databasecontext.h"
#include "dbpanelcontroller.h"
#include "rpipanelcontroller.h"
#include "ui/video/videowidget.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRectF>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget> // User Table
#include <algorithm>

static void populateReidTable(QTableWidget *table,
                              const QList<VehicleState> &vehicleStates,
                              int staleTimeoutMs, bool showStaleObjects);

MainWindowController::MainWindowController(const MainWindowUiRefs &uiRefs,
                                           QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
  // 而⑦듃濡ㅻ윭媛 ?섏쐞 ?쒕퉬?ㅼ쓽 ?섎챸???뚯쑀?쒕떎.
  // (QObject parent 愿怨꾨줈 MainWindow 醫낅즺 ???④퍡 ?뺣━??
  m_cameraManagerPrimary = new CameraManager(this);
  m_cameraManagerSecondary = new CameraManager(this);
  m_ocrCoordinatorPrimary = new PlateOcrCoordinator(this);
  m_ocrCoordinatorSecondary = new PlateOcrCoordinator(this);
  m_telegramApi = new TelegramBotAPI(this);
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
  m_parkingServicePrimary = new ParkingService(this);
  m_parkingServiceSecondary = new ParkingService(this);

  // ?몄뀡 ?쒕퉬?ㅻ뒗 "移대찓???쒖뼱 + 硫뷀??곗씠??吏???숆린??瑜?臾띕뒗 ?뚯궗????븷.
  m_cameraSessionPrimary.setCameraManager(m_cameraManagerPrimary);
  m_cameraSessionSecondary.setCameraManager(m_cameraManagerSecondary);
  const int delayMs = Config::instance().defaultDelayMs();
  m_cameraSessionPrimary.setDelayMs(delayMs);
  m_cameraSessionSecondary.setDelayMs(delayMs);
  refreshCameraConnectionFromConfig(m_cameraManagerPrimary,
                                    m_selectedCameraKeyPrimary,
                                    &m_selectedCameraKeyPrimary, true);
  refreshCameraConnectionFromConfig(m_cameraManagerSecondary,
                                    m_selectedCameraKeySecondary,
                                    &m_selectedCameraKeySecondary, false);

  // ?듯빀 DB 珥덇린??(veda.db)
  const QString dbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/veda.db");
  DatabaseContext::init(dbPath);

  // Parking ?쒕퉬??珥덇린??(DB Context ?ъ슜)
  QString parkingErrorPrimary;
  if (!m_parkingServicePrimary->init(&parkingErrorPrimary)) {
    qWarning() << "[Parking][A] Service init failed:" << parkingErrorPrimary;
  }
  QString parkingErrorSecondary;
  if (!m_parkingServiceSecondary->init(&parkingErrorSecondary)) {
    qWarning() << "[Parking][B] Service init failed:" << parkingErrorSecondary;
  }
  m_parkingServicePrimary->setTelegramApi(m_telegramApi);
  m_parkingServiceSecondary->setTelegramApi(m_telegramApi);

  // ROI DB 濡쒕뱶 -> UI 諛섏쁺 -> ?쒓렇???곌껐 ?쒖쑝濡?珥덇린??
  refreshCameraSelectors();
  if (m_parkingServicePrimary) {
    m_parkingServicePrimary->setCameraKey(m_selectedCameraKeyPrimary);
  }
  if (m_parkingServiceSecondary) {
    m_parkingServiceSecondary->setCameraKey(m_selectedCameraKeySecondary);
  }

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
    return m_roiServicePrimary.records();
  };
  dbContext.secondaryZoneRecordsProvider = [this]() {
    return m_roiServiceSecondary.records();
  };
  dbContext.logMessage = [this](const QString &message) { onLogMessage(message); };
  m_dbPanelController = new DbPanelController(dbUiRefs, dbContext, this);

  initRoiDbForChannels();
  refreshRoiSelectorForTarget();
  applyViewModeUiState();
  connectSignals();

  m_renderTimerPrimary.start();
  m_renderTimerSecondary.start();
}

void MainWindowController::shutdown() {
  QElapsedTimer timer;
  timer.start();

  const QString summary = m_logDeduplicator.flushPending();
  if (!summary.isEmpty()) {
    qDebug() << summary;
    if (m_ui.logView) {
      m_ui.logView->append(summary);
    }
  }
  m_cameraSessionPrimary.stop();
  m_cameraSessionSecondary.stop();
  if (m_rpiPanelController) {
    m_rpiPanelController->shutdown();
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
  // UI ?대깽??踰꾪듉/?꾩젽) -> Controller ?щ’ ?곌껐
  if (m_ui.btnPlay) {
    connect(m_ui.btnPlay, &QPushButton::clicked, this,
            &MainWindowController::playCctv);
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
  if (m_ui.viewModeCombo) {
    connect(m_ui.viewModeCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindowController::onViewModeChanged);
  }
  if (m_ui.cameraPrimarySelectorCombo) {
    connect(m_ui.cameraPrimarySelectorCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindowController::onCameraPrimarySelectionChanged);
  }
  if (m_ui.cameraSecondarySelectorCombo) {
    connect(m_ui.cameraSecondarySelectorCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindowController::onCameraSecondarySelectionChanged);
  }
  if (m_ui.videoWidgetPrimary) {
    connect(m_ui.videoWidgetPrimary, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(m_ui.videoWidgetPrimary, &VideoWidget::roiPolygonChanged, this,
            &MainWindowController::onRoiPolygonChanged);
    connect(m_ui.videoWidgetPrimary, &VideoWidget::ocrRequested,
            m_ocrCoordinatorPrimary,
            &PlateOcrCoordinator::requestOcr);
  }
  if (m_ui.videoWidgetSecondary) {
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiPolygonChanged, this,
            &MainWindowController::onRoiPolygonChanged);
    connect(m_ui.videoWidgetSecondary, &VideoWidget::ocrRequested,
            m_ocrCoordinatorSecondary,
            &PlateOcrCoordinator::requestOcr);
  }
  if (m_ui.reidTable) {
    connect(m_ui.reidTable, &QTableWidget::cellClicked, this,
            &MainWindowController::onReidTableCellClicked);
  }

  // 諛깆뿏???대깽??Camera/OCR) -> Controller ?щ’ ?곌껐
  connect(m_cameraManagerPrimary, &CameraManager::metadataReceived, this,
          &MainWindowController::onMetadataReceivedPrimary);
  connect(m_cameraManagerPrimary, &CameraManager::frameCaptured, this,
          &MainWindowController::onFrameCapturedPrimary);
  connect(m_cameraManagerPrimary, &CameraManager::logMessage, this,
          &MainWindowController::onLogMessage);
  connect(m_cameraManagerSecondary, &CameraManager::metadataReceived, this,
          &MainWindowController::onMetadataReceivedSecondary);
  connect(m_cameraManagerSecondary, &CameraManager::frameCaptured, this,
          &MainWindowController::onFrameCapturedSecondary);
  connect(m_cameraManagerSecondary, &CameraManager::logMessage, this,
          &MainWindowController::onLogMessage);
  connect(m_ocrCoordinatorPrimary, &PlateOcrCoordinator::ocrReady, this,
          &MainWindowController::onOcrResultPrimary);
  connect(m_ocrCoordinatorSecondary, &PlateOcrCoordinator::ocrReady, this,
          &MainWindowController::onOcrResultSecondary);

  // Telegram UI -> Controller
  if (m_ui.btnSendEntry) {
    connect(m_ui.btnSendEntry, &QPushButton::clicked, this,
            &MainWindowController::onSendEntry);
  }
  if (m_ui.btnSendExit) {
    connect(m_ui.btnSendExit, &QPushButton::clicked, this,
            &MainWindowController::onSendExit);
  }

  // Telegram API -> Controller
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

  // ParkingService -> Controller (濡쒓렇 ?대깽??
  connect(m_parkingServicePrimary, &ParkingService::logMessage, this,
          [this](const QString &msg) {
            onLogMessage(QString("[A] %1").arg(msg));
          });
  connect(m_parkingServiceSecondary, &ParkingService::logMessage, this,
          [this](const QString &msg) {
            onLogMessage(QString("[B] %1").arg(msg));
          });

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
  reloadRoiForTarget(RoiTarget::Primary, false);
  reloadRoiForTarget(RoiTarget::Secondary, false);
}

void MainWindowController::reloadRoiForTarget(RoiTarget target, bool writeLog) {
  RoiService *service = roiServiceForTarget(target);
  VideoWidget *widget = videoWidgetForTarget(target);
  if (!service || !widget) {
    return;
  }

  const QString cameraKey = cameraKeyForTarget(target);
  const RoiService::InitResult initResult = service->init(cameraKey);
  if (!initResult.ok) {
    if (m_ui.logView && writeLog) {
      m_ui.logView->append(
          QString("[ROI][DB] %1 珥덇린???ㅽ뙣: %2")
              .arg(target == RoiTarget::Primary ? "A" : "B", initResult.error));
    }
    return;
  }

  // 湲곗〈 ROI 珥덇린????移대찓?쇰퀎 ROI 濡쒕뱶
  widget->setUserRoi(QRect());
  QStringList roiLabels;
  const QVector<QJsonObject> &records = service->records();
  roiLabels.reserve(records.size());
  for (const QJsonObject &record : records) {
    roiLabels.append(record["rod_name"].toString().trimmed());
  }
  widget->queueNormalizedRoiPolygons(initResult.normalizedPolygons, roiLabels);

  if (m_ui.logView && writeLog) {
    m_ui.logView->append(QString("[ROI][DB] %1 梨꾨꼸 '%2' ROI %3媛?濡쒕뱶 ?꾨즺")
                             .arg(target == RoiTarget::Primary ? "A" : "B",
                                  cameraKey)
                             .arg(initResult.loadedCount));
  }
}

void MainWindowController::refreshRoiSelectorForTarget() {
  if (!m_ui.roiSelectorCombo) {
    return;
  }
  m_ui.roiSelectorCombo->clear();
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI ?좏깮"), -1);

  const RoiService *service = roiServiceForTarget(m_roiTarget);
  if (!service) {
    return;
  }

  const QVector<QJsonObject> &records = service->records();
  for (int i = 0; i < records.size(); ++i) {
    const QJsonObject &record = records[i];
    const QString name =
        record["rod_name"].toString(QString("rod_%1").arg(i + 1));
    const QString purpose = record["rod_purpose"].toString();
    m_ui.roiSelectorCombo->addItem(QString("%1 | %2").arg(name, purpose), i);
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

void MainWindowController::refreshRoiSelector() {
  refreshRoiSelectorForTarget();
}

VideoWidget *MainWindowController::videoWidgetForTarget(RoiTarget target) const {
  return (target == RoiTarget::Primary) ? m_ui.videoWidgetPrimary
                                        : m_ui.videoWidgetSecondary;
}

RoiService *MainWindowController::roiServiceForTarget(RoiTarget target) {
  return (target == RoiTarget::Primary) ? &m_roiServicePrimary
                                        : &m_roiServiceSecondary;
}

const RoiService *
MainWindowController::roiServiceForTarget(RoiTarget target) const {
  return (target == RoiTarget::Primary) ? &m_roiServicePrimary
                                        : &m_roiServiceSecondary;
}

ParkingService *MainWindowController::parkingServiceForTarget(RoiTarget target) {
  return (target == RoiTarget::Primary) ? m_parkingServicePrimary
                                        : m_parkingServiceSecondary;
}

QString MainWindowController::cameraKeyForTarget(RoiTarget target) const {
  return (target == RoiTarget::Primary) ? m_selectedCameraKeyPrimary
                                        : m_selectedCameraKeySecondary;
}

void MainWindowController::playCctv() {
  const bool primaryReady =
      refreshCameraConnectionFromConfig(m_cameraManagerPrimary,
                                        m_selectedCameraKeyPrimary,
                                        &m_selectedCameraKeyPrimary, true);
  if (!primaryReady) {
    onLogMessage("[Camera] ?곌껐 ?ㅼ젙???щ컮瑜댁? ?딆뒿?덈떎.");
    return;
  }
  if (m_parkingServicePrimary) {
    m_parkingServicePrimary->setCameraKey(m_selectedCameraKeyPrimary);
  }
  m_cameraSessionPrimary.playOrRestart();

  if (m_viewMode == ViewMode::Single) {
    m_cameraSessionSecondary.stop();
    return;
  }

  const bool secondaryReady =
      refreshCameraConnectionFromConfig(m_cameraManagerSecondary,
                                        m_selectedCameraKeySecondary,
                                        &m_selectedCameraKeySecondary, false);
  if (!secondaryReady) {
    onLogMessage(QString("[Camera] '%1' ?곌껐 ?ㅼ젙???щ컮瑜댁? ?딆븘 B 梨꾨꼸? 以묒??⑸땲??")
                     .arg(m_selectedCameraKeySecondary));
    m_cameraSessionSecondary.stop();
    return;
  }
  if (m_parkingServiceSecondary) {
    m_parkingServiceSecondary->setCameraKey(m_selectedCameraKeySecondary);
  }

  m_cameraSessionSecondary.playOrRestart();
}

void MainWindowController::refreshCameraSelectors() {
  if (!Config::instance().load()) {
    onLogMessage("Warning: could not reload config; using existing values.");
  }

  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  auto bindSelector = [&cameraKeys](QComboBox *combo, QString *selectedKey) {
    if (!combo || !selectedKey) {
      return;
    }

    QSignalBlocker blocker(combo);
    combo->clear();
    for (const QString &cameraKey : cameraKeys) {
      combo->addItem(cameraKey, cameraKey);
    }

    int selectIndex = combo->findData(*selectedKey);
    if (selectIndex < 0) {
      selectIndex = 0;
      *selectedKey = combo->itemData(0).toString();
    }
    combo->setCurrentIndex(selectIndex);
  };

  if (m_selectedCameraKeyPrimary.trimmed().isEmpty()) {
    m_selectedCameraKeyPrimary = QStringLiteral("camera");
  }
  bindSelector(m_ui.cameraPrimarySelectorCombo, &m_selectedCameraKeyPrimary);

  if (m_selectedCameraKeySecondary.trimmed().isEmpty()) {
    m_selectedCameraKeySecondary = QStringLiteral("camera2");
  }
  if (!cameraKeys.contains(m_selectedCameraKeySecondary)) {
    for (const QString &cameraKey : cameraKeys) {
      if (cameraKey != m_selectedCameraKeyPrimary) {
        m_selectedCameraKeySecondary = cameraKey;
        break;
      }
    }
  }
  bindSelector(m_ui.cameraSecondarySelectorCombo, &m_selectedCameraKeySecondary);
}

void MainWindowController::applyViewModeUiState() {
  if (m_ui.viewModeCombo) {
    QSignalBlocker blocker(m_ui.viewModeCombo);
    m_ui.viewModeCombo->setCurrentIndex(m_viewMode == ViewMode::Dual ? 1 : 0);
  }

  const bool dualMode = (m_viewMode == ViewMode::Dual);
  if (m_ui.cameraSecondarySelectorCombo) {
    m_ui.cameraSecondarySelectorCombo->setEnabled(dualMode);
  }
  if (m_ui.videoWidgetSecondary) {
    m_ui.videoWidgetSecondary->setVisible(dualMode);
  }
  if (m_ui.roiTargetCombo) {
    {
      QSignalBlocker blocker(m_ui.roiTargetCombo);
      m_ui.roiTargetCombo->setCurrentIndex(m_roiTarget == RoiTarget::Secondary
                                               ? 1
                                               : 0);
    }
    m_ui.roiTargetCombo->setEnabled(dualMode);
    if (!dualMode && m_ui.roiTargetCombo->currentIndex() != 0) {
      QSignalBlocker blocker(m_ui.roiTargetCombo);
      m_ui.roiTargetCombo->setCurrentIndex(0);
      m_roiTarget = RoiTarget::Primary;
      refreshRoiSelectorForTarget();
    }
  }
}

void MainWindowController::onRoiTargetChanged(int index) {
  const RoiTarget newTarget =
      (index == 1) ? RoiTarget::Secondary : RoiTarget::Primary;
  if (m_viewMode == ViewMode::Single && newTarget == RoiTarget::Secondary) {
    if (m_ui.roiTargetCombo) {
      QSignalBlocker blocker(m_ui.roiTargetCombo);
      m_ui.roiTargetCombo->setCurrentIndex(0);
    }
    m_roiTarget = RoiTarget::Primary;
    refreshRoiSelectorForTarget();
    return;
  }

  m_roiTarget = newTarget;
  refreshRoiSelectorForTarget();
  onLogMessage(QString("[ROI] ?몄쭛 ???蹂寃? %1")
                   .arg(m_roiTarget == RoiTarget::Primary ? "移대찓??A"
                                                          : "移대찓??B"));
}

bool MainWindowController::refreshCameraConnectionFromConfig(
    CameraManager *cameraManager, const QString &cameraKey,
    QString *resolvedKey, bool reloadConfig) {
  if (!cameraManager) {
    return false;
  }

  if (reloadConfig && !Config::instance().load()) {
    onLogMessage("Warning: could not reload config; using existing values.");
  }

  const auto &cfg = Config::instance();
  const QString selectedKey =
      cameraKey.trimmed().isEmpty() ? QStringLiteral("camera")
                                    : cameraKey.trimmed();
  CameraConnectionInfo connectionInfo;
  connectionInfo.cameraId = selectedKey;
  connectionInfo.ip = cfg.cameraIp(selectedKey).trimmed();
  connectionInfo.username = cfg.cameraUsername(selectedKey).trimmed();
  connectionInfo.password = cfg.cameraPassword(selectedKey);
  connectionInfo.profile = cfg.cameraProfile(selectedKey).trimmed();
  if (connectionInfo.profile.isEmpty()) {
    connectionInfo.profile = QStringLiteral("profile2/media.smp");
  }
  if (!connectionInfo.isValid()) {
    onLogMessage(QString("[Camera] '%1' ?ㅼ젙???좏슚?섏? ?딆뒿?덈떎. (ip/user)")
                     .arg(selectedKey));
    return false;
  }
  cameraManager->setConnectionInfo(connectionInfo);
  if (resolvedKey) {
    *resolvedKey = selectedKey;
  }
  return true;
}

void MainWindowController::onViewModeChanged(int index) {
  const ViewMode newMode = (index == 1) ? ViewMode::Dual : ViewMode::Single;
  if (m_viewMode == newMode) {
    applyViewModeUiState();
    return;
  }

  if (newMode == ViewMode::Dual &&
      m_selectedCameraKeyPrimary == m_selectedCameraKeySecondary) {
    if (!Config::instance().load()) {
      onLogMessage("Warning: could not reload config; using existing values.");
    }
    const QStringList cameraKeys = Config::instance().cameraKeys();
    QString alternateCameraKey;
    for (const QString &cameraKey : cameraKeys) {
      if (cameraKey != m_selectedCameraKeyPrimary) {
        alternateCameraKey = cameraKey;
        break;
      }
    }

    if (alternateCameraKey.isEmpty()) {
      onLogMessage(
          "[Camera] ???紐⑤뱶?먮뒗 ?쒕줈 ?ㅻⅨ 移대찓??2媛??ㅼ젙???꾩슂?⑸땲??");
      m_viewMode = ViewMode::Single;
      applyViewModeUiState();
      return;
    }

    m_selectedCameraKeySecondary = alternateCameraKey;
    if (m_ui.cameraSecondarySelectorCombo) {
      QSignalBlocker blocker(m_ui.cameraSecondarySelectorCombo);
      const int comboIndex =
          m_ui.cameraSecondarySelectorCombo->findData(alternateCameraKey);
      if (comboIndex >= 0) {
        m_ui.cameraSecondarySelectorCombo->setCurrentIndex(comboIndex);
      }
    }
    if (m_parkingServiceSecondary) {
      m_parkingServiceSecondary->setCameraKey(m_selectedCameraKeySecondary);
    }
    reloadRoiForTarget(RoiTarget::Secondary, false);
    refreshZoneTableAllChannels();
    if (m_roiTarget == RoiTarget::Secondary) {
      refreshRoiSelectorForTarget();
    }
    onLogMessage(QString("[Camera] B 梨꾨꼸 ?먮룞 蹂寃? %1")
                     .arg(m_selectedCameraKeySecondary));
  }

  m_viewMode = newMode;
  applyViewModeUiState();
  onLogMessage(QString("[Camera] 酉?紐⑤뱶 蹂寃? %1")
                   .arg(m_viewMode == ViewMode::Dual ? "2梨꾨꼸 ?숈떆"
                                                     : "1梨꾨꼸"));

  const bool wasRunningPrimary =
      m_cameraManagerPrimary && m_cameraManagerPrimary->isRunning();
  const bool wasRunningSecondary =
      m_cameraManagerSecondary && m_cameraManagerSecondary->isRunning();

  if (wasRunningPrimary || wasRunningSecondary) {
    playCctv();
  } else if (m_viewMode == ViewMode::Single) {
    m_cameraSessionSecondary.stop();
  }
}

void MainWindowController::onCameraPrimarySelectionChanged(int index) {
  if (!m_ui.cameraPrimarySelectorCombo || index < 0) {
    return;
  }

  const QString previousKey = m_selectedCameraKeyPrimary;
  const QString selectedKey =
      m_ui.cameraPrimarySelectorCombo->itemData(index).toString();
  if (selectedKey.isEmpty() || selectedKey == m_selectedCameraKeyPrimary) {
    return;
  }
  if (m_viewMode == ViewMode::Dual &&
      selectedKey == m_selectedCameraKeySecondary) {
    onLogMessage(
        "[Camera] A/B 梨꾨꼸? ?쒕줈 ?ㅻⅨ 移대찓?쇰? ?좏깮?댁빞 ?⑸땲??");
    QSignalBlocker blocker(m_ui.cameraPrimarySelectorCombo);
    const int previousIndex = m_ui.cameraPrimarySelectorCombo->findData(previousKey);
    if (previousIndex >= 0) {
      m_ui.cameraPrimarySelectorCombo->setCurrentIndex(previousIndex);
    }
    return;
  }
  m_selectedCameraKeyPrimary = selectedKey;
  if (m_parkingServicePrimary) {
    m_parkingServicePrimary->setCameraKey(m_selectedCameraKeyPrimary);
  }
  onLogMessage(
      QString("[Camera] A 梨꾨꼸 蹂寃? %1").arg(m_selectedCameraKeyPrimary));
  reloadRoiForTarget(RoiTarget::Primary);
  refreshZoneTableAllChannels();
  if (m_roiTarget == RoiTarget::Primary) {
    refreshRoiSelectorForTarget();
  }

  if (m_cameraManagerPrimary && m_cameraManagerPrimary->isRunning() &&
      refreshCameraConnectionFromConfig(m_cameraManagerPrimary,
                                        m_selectedCameraKeyPrimary,
                                        &m_selectedCameraKeyPrimary)) {
    m_cameraSessionPrimary.playOrRestart();
  }
}

void MainWindowController::onCameraSecondarySelectionChanged(int index) {
  if (!m_ui.cameraSecondarySelectorCombo || index < 0) {
    return;
  }

  const QString previousKey = m_selectedCameraKeySecondary;
  const QString selectedKey =
      m_ui.cameraSecondarySelectorCombo->itemData(index).toString();
  if (selectedKey.isEmpty() || selectedKey == m_selectedCameraKeySecondary) {
    return;
  }
  if (m_viewMode == ViewMode::Dual &&
      selectedKey == m_selectedCameraKeyPrimary) {
    onLogMessage(
        "[Camera] A/B 梨꾨꼸? ?쒕줈 ?ㅻⅨ 移대찓?쇰? ?좏깮?댁빞 ?⑸땲??");
    QSignalBlocker blocker(m_ui.cameraSecondarySelectorCombo);
    const int previousIndex =
        m_ui.cameraSecondarySelectorCombo->findData(previousKey);
    if (previousIndex >= 0) {
      m_ui.cameraSecondarySelectorCombo->setCurrentIndex(previousIndex);
    }
    return;
  }
  m_selectedCameraKeySecondary = selectedKey;
  if (m_parkingServiceSecondary) {
    m_parkingServiceSecondary->setCameraKey(m_selectedCameraKeySecondary);
  }
  onLogMessage(
      QString("[Camera] B 梨꾨꼸 蹂寃? %1").arg(m_selectedCameraKeySecondary));
  reloadRoiForTarget(RoiTarget::Secondary);
  refreshZoneTableAllChannels();
  if (m_roiTarget == RoiTarget::Secondary) {
    refreshRoiSelectorForTarget();
  }

  if (m_viewMode != ViewMode::Dual) {
    return;
  }
  if (m_cameraManagerSecondary && m_cameraManagerSecondary->isRunning() &&
      refreshCameraConnectionFromConfig(m_cameraManagerSecondary,
                                        m_selectedCameraKeySecondary,
                                        &m_selectedCameraKeySecondary)) {
    m_cameraSessionSecondary.playOrRestart();
  }
}

void MainWindowController::updateObjectFilter(
    const QSet<QString> &disabledTypes) {
  if (m_cameraManagerPrimary) {
    m_cameraManagerPrimary->setDisabledObjectTypes(disabledTypes);
  }
  if (m_cameraManagerSecondary) {
    m_cameraManagerSecondary->setDisabledObjectTypes(disabledTypes);
  }
}

void MainWindowController::onLogMessage(const QString &msg) {
  if (!m_ui.logView) {
    return;
  }

  // 踰덊샇???몄떇 濡쒓렇 ?꾪꽣留?(UI留?臾댁떆, qDebug??異쒕젰)
  bool showInUi = true;
  if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
    if (msg.startsWith(QStringLiteral("[Parking]"))) {
      showInUi = false;
    }
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const LogDeduplicator::IngestResult ingestResult =
      m_logDeduplicator.ingest(msg, nowMs);

  if (!ingestResult.flushSummary.isEmpty()) {
    qDebug() << ingestResult.flushSummary;
    if (showInUi) {
      m_ui.logView->append(ingestResult.flushSummary);
    }
  }
  if (ingestResult.suppressed) {
    return;
  }

  qDebug() << "[Camera]" << msg;
  if (showInUi) {
    m_ui.logView->append(msg);
  }
}

void MainWindowController::onOcrResultPrimary(int objectId,
                                              const QString &result) {
  if (!m_ui.logView) {
    return;
  }
  const QString msg =
      QString("[OCR][A] ID:%1 Result:%2").arg(objectId).arg(result);
  qDebug() << msg;

  // 踰덊샇???몄떇 濡쒓렇 ?꾪꽣留?  if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
    return;
  }
  m_ui.logView->append(msg);

  // OCR 寃곌낵瑜?ParkingService???꾨떖?섏뿬 DB 湲곕줉 + ?뚮┝ 泥섎━
  if (m_parkingServicePrimary) {
    m_parkingServicePrimary->processOcrResult(objectId, result);
  }
}

void MainWindowController::onOcrResultSecondary(int objectId,
                                                const QString &result) {
  if (!m_ui.logView) {
    return;
  }
  const QString msg =
      QString("[OCR][B] ID:%1 Result:%2").arg(objectId).arg(result);
  qDebug() << msg;

  if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
    return;
  }
  m_ui.logView->append(msg);

  if (m_parkingServiceSecondary) {
    m_parkingServiceSecondary->processOcrResult(objectId, result);
  }
}

void MainWindowController::onStartRoiDraw() {
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  if (!targetWidget || !m_ui.logView) {
    return;
  }
  targetWidget->startRoiDrawing();
  m_ui.logView->append(
      QString("[ROI] Draw mode (%1): left-click points, then press 'ROI ?꾨즺'.")
          .arg(m_roiTarget == RoiTarget::Primary ? "移대찓??A" : "移대찓??B"));
}

void MainWindowController::onCompleteRoiDraw() {
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  RoiService *targetService = roiServiceForTarget(m_roiTarget);
  if (!targetWidget || !targetService || !m_ui.logView) {
    return;
  }
  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  QString nameError;
  if (!targetService->isValidName(typedName, &nameError)) {
    m_ui.logView->append(QString("[ROI] ?꾨즺 ?ㅽ뙣: %1").arg(nameError));
    return;
  }
  if (targetService->isDuplicateName(typedName)) {
    m_ui.logView->append(
        QString("[ROI] ?꾨즺 ?ㅽ뙣: ?대쫫 '%1' ??媛) ?대? 議댁옱?⑸땲??")
            .arg(typedName));
    return;
  }

  // ?ㅼ젣 ?대━怨??꾨즺??VideoWidget?먯꽌 泥섎━?섍퀬,
  // ?깃났 ??roiPolygonChanged ?쒓렇?먯씠 ?ㅼ떆 而⑦듃濡ㅻ윭濡??щ씪?⑤떎.
  if (!targetWidget->completeRoiDrawing()) {
    m_ui.logView->append("[ROI] ?꾨즺 ?ㅽ뙣: 理쒖냼 3媛??먯씠 ?꾩슂?⑸땲??");
  }
}

void MainWindowController::onDeleteSelectedRoi() {
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  RoiService *targetService = roiServiceForTarget(m_roiTarget);
  if (!m_ui.roiSelectorCombo || !targetWidget || !targetService ||
      !m_ui.logView) {
    return;
  }
  // 肄ㅻ낫諛뺤뒪?먯꽌 ?꾩옱 ?좏깮???몃뜳???뺤씤
  if (m_ui.roiSelectorCombo->currentIndex() < 0) {
    return;
  }
  const int recordIndex = m_ui.roiSelectorCombo->currentData().toInt();
  if (recordIndex < 0 || recordIndex >= targetService->count()) {
    m_ui.logView->append("[ROI] ??젣 ?ㅽ뙣: ROI瑜??좏깮?댁＜?몄슂.");
    return;
  }

  const RoiService::DeleteResult deleteResult =
      targetService->removeAt(recordIndex);
  if (!deleteResult.ok) {
    m_ui.logView->append(
        QString("[ROI][DB] ??젣 ?ㅽ뙣: %1").arg(deleteResult.error));
    return;
  }
  if (!targetWidget->removeRoiAt(recordIndex)) {
    m_ui.logView->append(
        "[ROI] ??젣 ?ㅽ뙣: ROI ?곹깭? 紐⑸줉???쇱튂?섏? ?딆뒿?덈떎.");
    return;
  }

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
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[ROI] ??젣 ?꾨즺: %1").arg(deleteResult.removedName));
  }
}

void MainWindowController::onRoiChanged(const QRect &roi) {
  if (!m_ui.logView) {
    return;
  }
  const VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  const QString channel =
      (sourceWidget == m_ui.videoWidgetSecondary) ? QStringLiteral("B")
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
    m_ui.logView->append("[ROI] ????ㅽ뙣: ?꾨젅???ш린媛 ?좏슚?섏? ?딆뒿?덈떎.");
    return;
  }

  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  const QString purpose =
      m_ui.roiPurposeCombo ? m_ui.roiPurposeCombo->currentText() : QString();

  VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  RoiTarget target = m_roiTarget;
  if (sourceWidget == m_ui.videoWidgetSecondary) {
    target = RoiTarget::Secondary;
  } else if (sourceWidget == m_ui.videoWidgetPrimary) {
    target = RoiTarget::Primary;
  }

  RoiService *targetService = roiServiceForTarget(target);
  VideoWidget *targetWidget = videoWidgetForTarget(target);
  if (!targetService || !targetWidget) {
    return;
  }

  const RoiService::CreateResult createResult =
      targetService->createFromPolygon(polygon, frameSize, typedName, purpose);
  if (!createResult.ok) {
    // UI?먮뒗 ?대? 諛⑷툑 洹몃┛ ROI媛 異붽??섏뼱 ?덉쓣 ???덉쑝誘濡?濡ㅻ갚 泥섎━.
    if (targetWidget->roiCount() > 0) {
      targetWidget->removeRoiAt(targetWidget->roiCount() - 1);
    }
    m_ui.logView->append(
        QString("[ROI][DB] ????ㅽ뙣: %1").arg(createResult.error));
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
  if (targetWidget) {
    const int recordIndex = targetService->count() - 1;
    targetWidget->setRoiLabelAt(
        recordIndex, createResult.record["rod_name"].toString().trimmed());
  }
  appendRoiStructuredLog(createResult.record);
}

void MainWindowController::onMetadataReceivedPrimary(
    const QList<ObjectInfo> &objects) {
  // 硫뷀??곗씠?곕뒗 利됱떆 ?뚮뜑?섏? ?딄퀬 ??꾩뒪?ы봽? ?④퍡 ?먯뿉 ?ｋ뒗??
  // ?꾨젅???꾩갑 ?쒖젏??吏?곌컪(delay)??諛섏쁺??爰쇰궡 ?곌린 ?꾪븿.
  m_cameraSessionPrimary.pushMetadata(objects,
                                      QDateTime::currentMSecsSinceEpoch());

  // ParkingService?먮룄 硫뷀??곗씠?곕? ?꾨떖?섏뿬 ?낆텧李?媛먯? ?섑뻾
  // 留??꾨젅?꾨쭏??ROI ?대━怨ㅼ쓣 ?숆린??(DB 濡쒕뱶/?ъ슜??洹몃━湲?諛섏쁺)
  if (m_ui.videoWidgetPrimary && m_parkingServicePrimary) {
    m_parkingServicePrimary->updateRoiPolygons(
        m_ui.videoWidgetPrimary->roiPolygons());
  }
  const auto &cfg = Config::instance();
  int pruneMs = m_ui.pruneTimeoutInput ? m_ui.pruneTimeoutInput->value() : 5000;
  if (m_parkingServicePrimary) {
    m_parkingServicePrimary->processMetadata(objects, cfg.effectiveWidth(),
                                             cfg.sourceHeight(), pruneMs);
  }

  if (m_ui.reidTable && m_roiTarget == RoiTarget::Primary &&
      m_parkingServicePrimary) {
    const int staleMs =
        m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;
    const bool showStaleObjects =
        !m_ui.chkShowStaleObjects || m_ui.chkShowStaleObjects->isChecked();
    populateReidTable(m_ui.reidTable, m_parkingServicePrimary->activeVehicles(),
                      staleMs, showStaleObjects);
  }
}

void MainWindowController::onMetadataReceivedSecondary(
    const QList<ObjectInfo> &objects) {
  m_cameraSessionSecondary.pushMetadata(objects,
                                        QDateTime::currentMSecsSinceEpoch());

  if (m_ui.videoWidgetSecondary && m_parkingServiceSecondary) {
    m_parkingServiceSecondary->updateRoiPolygons(
        m_ui.videoWidgetSecondary->roiPolygons());
  }

  const auto &cfg = Config::instance();
  int pruneMs = m_ui.pruneTimeoutInput ? m_ui.pruneTimeoutInput->value() : 5000;
  if (m_parkingServiceSecondary) {
    m_parkingServiceSecondary->processMetadata(objects, cfg.effectiveWidth(),
                                               cfg.sourceHeight(), pruneMs);
  }

  if (m_ui.reidTable && m_roiTarget == RoiTarget::Secondary &&
      m_parkingServiceSecondary) {
    const int staleMs =
        m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;
    const bool showStaleObjects =
        !m_ui.chkShowStaleObjects || m_ui.chkShowStaleObjects->isChecked();
    populateReidTable(m_ui.reidTable,
                      m_parkingServiceSecondary->activeVehicles(), staleMs,
                      showStaleObjects);
  }
}

void MainWindowController::onFrameCapturedPrimary(
    QSharedPointer<cv::Mat> framePtr, qint64 timestampMs) {
  if (!m_ui.videoWidgetPrimary || !framePtr || framePtr->empty()) {
    return;
  }

  // === UI Frame Throttling ===
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  // 1. Stale Frame Drop (?ㅼ떆媛꾩꽦 ?뺣낫 諛?OOM 諛⑹?)
  // ?먯뿉 ?덈Т ?ㅻ옒 癒몃Ъ???덈뜕 ?꾨젅?꾩? 踰꾨┝ (?? 60ms ?댁긽 吏?곕맂 ?꾨젅??
  // 踰꾨젮吏??꾨젅?꾩뿉 ??댁꽌??硫붾え由??좊떦/蹂듭궗(Overhead)媛 ?꾪? 諛쒖깮?섏? ?딆쓬
  // (Zero-Copy)
  if ((nowMs - timestampMs) > 60) {
    return;
  }

  // 2. Render Throttling (UI ?뚮뜑留?遺??諛⑹?)
  // ??30~33fps ?섏???30ms濡??쒗븳?섏뿬 遺?섏? 遺?쒕윭?????묒젏 ?곸슜
  if (m_renderTimerPrimary.isValid() && m_renderTimerPrimary.elapsed() < 30) {
    return;
  }
  m_renderTimerPrimary.restart();

  // === ?뚮뜑留곹븷 ?꾨젅?꾩뿉留?BGR ??RGB ?됱긽 蹂???섑뻾 ===
  // 鍮꾨뵒???ㅻ젅?쒖뿉??紐⑤뱺 ?꾨젅?꾩뿉 蹂?섑븯硫?遺덊븘?뷀븳 CPU ?뚮え媛 諛쒖깮?섎?濡?
  // Throttle/Stale ?먯젙???듦낵???꾨젅??珥덈떦 ~33媛??먮쭔 蹂?섏쓣 ?곸슜?⑸땲??
  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);

  // === Zero-Copy QImage ?섑븨 ===
  // BGR?뭃GB 蹂???꾨즺 ?? OpenCV 踰꾪띁 ?곗씠?곕? 蹂듭궗?섏? ?딄퀬
  // 爾먮떎留?蹂대뒗(shallow copy) QImage 媛앹껜瑜??앹꽦?⑸땲??
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);

  // ?꾨젅??媛깆떊 吏곸쟾??"?꾩옱 ?쒓컖 湲곗??쇰줈 以鍮꾨맂 硫뷀??곗씠??留??뚮퉬?쒕떎.
  // ?대젃寃??댁빞 諛뺤뒪/媛앹껜 ?뺣낫? 鍮꾨뵒???꾨젅?꾩씠 ???먯뿰?ㅻ읇寃?留욌뒗??
  m_ui.videoWidgetPrimary->updateMetadata(
      m_cameraSessionPrimary.consumeReadyMetadata(nowMs));

  // QImage 李몄“ ?꾨떖.
  // ?대??먯꽌 scaled ?섍굅???ъ슜(蹂듭궗)????qimg ?ㅼ퐫??醫낅즺 ??framePtr.reset()
  // ?ㅽ뻾??
  m_ui.videoWidgetPrimary->updateFrame(qimg);
}

void MainWindowController::onFrameCapturedSecondary(
    QSharedPointer<cv::Mat> framePtr, qint64 timestampMs) {
  if (!m_ui.videoWidgetSecondary || !framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > 60) {
    return;
  }
  if (m_renderTimerSecondary.isValid() &&
      m_renderTimerSecondary.elapsed() < 30) {
    return;
  }
  m_renderTimerSecondary.restart();

  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);

  m_ui.videoWidgetSecondary->updateMetadata(
      m_cameraSessionSecondary.consumeReadyMetadata(nowMs));
  m_ui.videoWidgetSecondary->updateFrame(qimg);
}

void MainWindowController::onSendEntry() {
  if (!m_ui.entryPlateInput || !m_ui.logView)
    return;

  QString plate = m_ui.entryPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    m_ui.logView->append("[Telegram] 李⑤웾踰덊샇瑜??낅젰?댁＜?몄슂.");
    return;
  }
  m_telegramApi->sendEntryNotice(plate);
}

void MainWindowController::onSendExit() {
  if (!m_ui.exitPlateInput || !m_ui.feeInput || !m_ui.logView)
    return;

  QString plate = m_ui.exitPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    m_ui.logView->append("[Telegram] 李⑤웾踰덊샇瑜??낅젰?댁＜?몄슂.");
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
    m_ui.userCountLabel->setText(QString("%1 紐?).arg(count));
  }

  // Update Table
  if (m_ui.userTable && m_telegramApi) {
    QMap<QString, QString> users = m_telegramApi->getRegisteredUsers();
    m_ui.userTable->setRowCount(0); // Clear
    for (auto it = users.begin(); it != users.end(); ++it) {
      int row = m_ui.userTable->rowCount();
      m_ui.userTable->insertRow(row);
      m_ui.userTable->setItem(row, 0,
                              new QTableWidgetItem(it.key())); // Chat ID
      m_ui.userTable->setItem(row, 1,
                              new QTableWidgetItem(it.value())); // Plate
    }
  }

  // DB ??쓽 ?ъ슜???뚯씠釉붾룄 ?④퍡 媛깆떊
  if (m_dbPanelController) {
    m_dbPanelController->refreshUserTable();
  }
}

void MainWindowController::onPaymentConfirmed(const QString &plate,
                                              int amount) {
  if (m_ui.logView) {
    const QString msg =
        QString("[Payment] ?뮥 寃곗젣 ?꾨즺 ?섏떊! 李⑤웾: %1, 湲덉븸: %2??)
            .arg(plate)
            .arg(amount);
    qDebug() << msg;

    // 踰덊샇???몄떇 濡쒓렇 ?꾪꽣留?    if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
      return;
    }
    m_ui.logView->append(msg);
  }
}

// ?? Parking DB Panel Slots ??????????????????????????????????????????

static void populateReidTable(QTableWidget *table,
                              const QList<VehicleState> &vehicleStates,
                              int staleTimeoutMs, bool showStaleObjects) {
  if (!table) {
    return;
  }

  table->setRowCount(0);

  QList<VehicleState> sortedVs = vehicleStates;
  std::sort(sortedVs.begin(), sortedVs.end(),
            [](const VehicleState &a, const VehicleState &b) {
              return a.objectId < b.objectId;
            });

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  for (const VehicleState &vs : sortedVs) {
    if (vs.objectId < 0) {
      continue;
    }

    const bool isStale = (nowMs - vs.lastSeenMs) > staleTimeoutMs;
    if (isStale && !showStaleObjects) {
      continue;
    }

    const int row = table->rowCount();
    table->insertRow(row);

    const QColor textColor = isStale ? Qt::gray : Qt::black;

    auto *idItem = new QTableWidgetItem(QString::number(vs.objectId));
    idItem->setForeground(textColor);
    table->setItem(row, 0, idItem);

    auto *typeItem = new QTableWidgetItem(vs.type);
    typeItem->setForeground(textColor);
    table->setItem(row, 1, typeItem);

    auto *plateItem = new QTableWidgetItem(vs.plateNumber);
    plateItem->setForeground(textColor);
    table->setItem(row, 2, plateItem);

    auto *scoreItem = new QTableWidgetItem(QString::number(vs.score, 'f', 2));
    scoreItem->setForeground(textColor);
    table->setItem(row, 3, scoreItem);

    const QRectF &rect = vs.boundingBox;
    auto *bboxItem = new QTableWidgetItem(QString("x:%1 y:%2 w:%3 h:%4")
                                              .arg(rect.x(), 0, 'f', 1)
                                              .arg(rect.y(), 0, 'f', 1)
                                              .arg(rect.width(), 0, 'f', 1)
                                              .arg(rect.height(), 0, 'f', 1));
    bboxItem->setForeground(textColor);
    table->setItem(row, 4, bboxItem);
  }
}

void MainWindowController::onReidTableCellClicked(int row, int column) {
  Q_UNUSED(column);
  if (!m_ui.reidTable)
    return;

  QTableWidgetItem *idItem = m_ui.reidTable->item(row, 0);
  QTableWidgetItem *plateItem = m_ui.reidTable->item(row, 2);

  if (idItem && m_ui.forceObjectIdInput) {
    m_ui.forceObjectIdInput->setValue(idItem->text().toInt());
  }

  // Type
  QTableWidgetItem *typeItem = m_ui.reidTable->item(row, 1);
  if (typeItem && m_ui.forceTypeInput) {
    m_ui.forceTypeInput->setText(typeItem->text());
  }

  // Plate
  if (plateItem && m_ui.forcePlateInput) {
    m_ui.forcePlateInput->setText(plateItem->text());
  }

  // Score
  QTableWidgetItem *scoreItem = m_ui.reidTable->item(row, 3);
  if (scoreItem && m_ui.forceScoreInput) {
    m_ui.forceScoreInput->setValue(scoreItem->text().toDouble());
  }

  // BBox
  QTableWidgetItem *bboxItem = m_ui.reidTable->item(row, 4);
  if (bboxItem && m_ui.forceBBoxInput) {
    m_ui.forceBBoxInput->setText(bboxItem->text());
  }
}

void MainWindowController::onAdminSummoned(const QString &chatId,
                                           const QString &name) {
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[?뚮┝] ?슚 愿由ъ옄 ?몄텧 ?섏떊! (User: %1, ChatID: %2)")
            .arg(name, chatId));
  }

  QMessageBox::information(
      nullptr, "愿由ъ옄 ?몄텧",
      QString("?슚 ?ъ슜?먭? 愿由ъ옄瑜??몄텧?덉뒿?덈떎!\n\n?대쫫: %1\nChat ID: %2")
          .arg(name, chatId));
}
