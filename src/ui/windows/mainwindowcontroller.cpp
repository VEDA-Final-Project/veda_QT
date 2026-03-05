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
#include <QEvent>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRectF>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget> // User Table
#include <QTimer>
#include <algorithm>

static void populateReidTable(QTableWidget *table,
                              const QList<VehicleState> &vehicleStates,
                              int staleTimeoutMs, bool showStaleObjects);

MainWindowController::MainWindowController(const MainWindowUiRefs &uiRefs,
                                           QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
  // 컨트롤러가 하위 서비스의 수명을 소유한다.
  // (QObject parent 관계로 MainWindow 종료 시 함께 정리됨)
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

  // 세션 서비스는 "카메라 제어 + 메타데이터 지연 동기화"를 묶는 파사드 역할.
  m_cameraSessionPrimary.setCameraManager(m_cameraManagerPrimary);
  m_cameraSessionSecondary.setCameraManager(m_cameraManagerSecondary);
  const int delayMs = Config::instance().defaultDelayMs();
  m_cameraSessionPrimary.setDelayMs(delayMs);
  m_cameraSessionSecondary.setDelayMs(delayMs);
  refreshCameraConnectionFromConfig(
      m_cameraManagerPrimary, m_selectedCameraKeyPrimary,
      &m_selectedCameraKeyPrimary, QString(), true);
  refreshCameraConnectionFromConfig(
      m_cameraManagerSecondary, m_selectedCameraKeySecondary,
      &m_selectedCameraKeySecondary, QString(), false);

  // 통합 DB 초기화 (veda.db)
  const QString dbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/veda.db");
  DatabaseContext::init(dbPath);

  // Parking 서비스 초기화 (DB Context 사용)
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

  // ROI DB 로드 -> UI 반영 -> 시그널 연결 순으로 초기화.
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
  dbContext.logMessage = [this](const QString &message) {
    onLogMessage(message);
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

  initRoiDbForChannels();
  refreshRoiSelectorForTarget();
  applyViewModeUiState();
  connectSignals();

  m_renderTimerPrimary.start();
  m_renderTimerSecondary.start();
}

bool MainWindowController::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::Resize) {
    if (obj == m_ui.videoWidgetPrimary || obj == m_ui.videoWidgetSecondary) {
      m_resizeDebounceTimer->start(500); // 500ms debounce
    }
  }
  return QObject::eventFilter(obj, event);
}

void MainWindowController::onVideoWidgetResizedSlot() {
  if (m_viewMode == ViewMode::Single) {
    if (m_ui.videoWidgetPrimary && m_cameraManagerPrimary &&
        m_cameraManagerPrimary->isRunning()) {
      QSize size = m_ui.videoWidgetPrimary->size();
      QString bestProfile = getBestProfileForSize(size);
      if (bestProfile != m_currentProfilePrimary) {
        onLogMessage(
            QString("[Camera] 화면 크기 변경 감지 (A 채널 %1x%2) -> %3 적용")
                .arg(size.width())
                .arg(size.height())
                .arg(bestProfile));
        m_currentProfilePrimary = bestProfile;
        if (refreshCameraConnectionFromConfig(
                m_cameraManagerPrimary, m_selectedCameraKeyPrimary,
                &m_selectedCameraKeyPrimary, m_currentProfilePrimary, false)) {
          m_cameraSessionPrimary.playOrRestart();
        }
      }
    }
  } else if (m_viewMode == ViewMode::Dual) {
    if (m_ui.videoWidgetPrimary && m_cameraManagerPrimary &&
        m_cameraManagerPrimary->isRunning()) {
      QSize sizeA = m_ui.videoWidgetPrimary->size();
      QString bestProfileA = getBestProfileForSize(sizeA);
      if (bestProfileA != m_currentProfilePrimary) {
        onLogMessage(
            QString("[Camera] 화면 크기 변경 감지 (A 채널 %1x%2) -> %3 적용")
                .arg(sizeA.width())
                .arg(sizeA.height())
                .arg(bestProfileA));
        m_currentProfilePrimary = bestProfileA;
        if (refreshCameraConnectionFromConfig(
                m_cameraManagerPrimary, m_selectedCameraKeyPrimary,
                &m_selectedCameraKeyPrimary, m_currentProfilePrimary, false)) {
          m_cameraSessionPrimary.playOrRestart();
        }
      }
    }
    if (m_ui.videoWidgetSecondary && m_cameraManagerSecondary &&
        m_cameraManagerSecondary->isRunning()) {
      QSize sizeB = m_ui.videoWidgetSecondary->size();
      QString bestProfileB = getBestProfileForSize(sizeB);
      if (bestProfileB != m_currentProfileSecondary) {
        onLogMessage(
            QString("[Camera] 화면 크기 변경 감지 (B 채널 %1x%2) -> %3 적용")
                .arg(sizeB.width())
                .arg(sizeB.height())
                .arg(bestProfileB));
        m_currentProfileSecondary = bestProfileB;
        if (refreshCameraConnectionFromConfig(
                m_cameraManagerSecondary, m_selectedCameraKeySecondary,
                &m_selectedCameraKeySecondary, m_currentProfileSecondary,
                false)) {
          m_cameraSessionSecondary.playOrRestart();
        }
      }
    }
  }
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
  // UI 이벤트(버튼/위젯) -> Controller 슬롯 연결
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
  if (m_ui.btnRunBenchmark) {
    connect(m_ui.btnRunBenchmark, &QPushButton::clicked, this,
            &MainWindowController::onRunBenchmark);
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
            m_ocrCoordinatorPrimary, &PlateOcrCoordinator::requestOcr);
    connect(m_ui.videoWidgetPrimary, &VideoWidget::avgFpsUpdated, this,
            [this](double fps) {
              if (m_ui.lblAvgFps) {
                m_ui.lblAvgFps->setText(
                    QString("최근 1분 평균 FPS: %1").arg(fps, 0, 'f', 1));
              }
            });
  }
  if (m_ui.videoWidgetSecondary) {
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiPolygonChanged, this,
            &MainWindowController::onRoiPolygonChanged);
    connect(m_ui.videoWidgetSecondary, &VideoWidget::ocrRequested,
            m_ocrCoordinatorSecondary, &PlateOcrCoordinator::requestOcr);
  }
  if (m_ui.reidTable) {
    connect(m_ui.reidTable, &QTableWidget::cellClicked, this,
            &MainWindowController::onReidTableCellClicked);
  }
  if (m_ui.chkShowFps) {
    connect(m_ui.chkShowFps, &QCheckBox::toggled, this, [this](bool checked) {
      if (m_ui.videoWidgetPrimary)
        m_ui.videoWidgetPrimary->setShowFps(checked);
      if (m_ui.videoWidgetSecondary)
        m_ui.videoWidgetSecondary->setShowFps(checked);
    });
  }

  // 백엔드 이벤트(Camera/OCR) -> Controller 슬롯 연결
  connect(m_cameraManagerPrimary, &CameraManager::metadataReceived, this,
          &MainWindowController::onMetadataReceivedPrimary);
  connect(m_cameraManagerPrimary, &CameraManager::frameCaptured, this,
          &MainWindowController::onFrameCapturedPrimary);
  connect(m_cameraManagerPrimary, &CameraManager::ocrFrameCaptured, this,
          &MainWindowController::onOcrFrameCapturedPrimary);
  connect(m_cameraManagerPrimary, &CameraManager::logMessage, this,
          &MainWindowController::onLogMessage);
  connect(m_cameraManagerSecondary, &CameraManager::metadataReceived, this,
          &MainWindowController::onMetadataReceivedSecondary);
  connect(m_cameraManagerSecondary, &CameraManager::frameCaptured, this,
          &MainWindowController::onFrameCapturedSecondary);
  connect(m_cameraManagerSecondary, &CameraManager::ocrFrameCaptured, this,
          &MainWindowController::onOcrFrameCapturedSecondary);
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

  // ParkingService -> Controller (로그 이벤트)
  connect(
      m_parkingServicePrimary, &ParkingService::logMessage, this,
      [this](const QString &msg) { onLogMessage(QString("[A] %1").arg(msg)); });
  connect(
      m_parkingServiceSecondary, &ParkingService::logMessage, this,
      [this](const QString &msg) { onLogMessage(QString("[B] %1").arg(msg)); });

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
          QString("[ROI][DB] %1 초기화 실패: %2")
              .arg(target == RoiTarget::Primary ? "A" : "B", initResult.error));
    }
    return;
  }

  // 기존 ROI 초기화 후 카메라별 ROI 로드
  widget->setUserRoi(QRect());
  QStringList roiLabels;
  const QVector<QJsonObject> &records = service->records();
  roiLabels.reserve(records.size());
  for (const QJsonObject &record : records) {
    roiLabels.append(record["zone_name"].toString().trimmed());
  }
  widget->queueNormalizedRoiPolygons(initResult.normalizedPolygons, roiLabels);

  if (m_ui.logView && writeLog) {
    m_ui.logView->append(
        QString("[ROI][DB] %1 채널 '%2' ROI %3개 로드 완료")
            .arg(target == RoiTarget::Primary ? "A" : "B", cameraKey)
            .arg(initResult.loadedCount));
  }
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

void MainWindowController::refreshRoiSelector() {
  refreshRoiSelectorForTarget();
}

VideoWidget *
MainWindowController::videoWidgetForTarget(RoiTarget target) const {
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

ParkingService *
MainWindowController::parkingServiceForTarget(RoiTarget target) {
  return (target == RoiTarget::Primary) ? m_parkingServicePrimary
                                        : m_parkingServiceSecondary;
}

QString MainWindowController::cameraKeyForTarget(RoiTarget target) const {
  return (target == RoiTarget::Primary) ? m_selectedCameraKeyPrimary
                                        : m_selectedCameraKeySecondary;
}

void MainWindowController::playCctv() {
  QString profileA;
  if (m_ui.videoWidgetPrimary) {
    profileA = getBestProfileForSize(m_ui.videoWidgetPrimary->size());
    m_currentProfilePrimary = profileA;
  }

  const bool primaryReady = refreshCameraConnectionFromConfig(
      m_cameraManagerPrimary, m_selectedCameraKeyPrimary,
      &m_selectedCameraKeyPrimary, profileA, true);
  if (!primaryReady) {
    onLogMessage("[Camera] 연결 설정이 올바르지 않습니다.");
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

  QString profileB;
  if (m_ui.videoWidgetSecondary) {
    profileB = getBestProfileForSize(m_ui.videoWidgetSecondary->size());
    m_currentProfileSecondary = profileB;
  }

  const bool secondaryReady = refreshCameraConnectionFromConfig(
      m_cameraManagerSecondary, m_selectedCameraKeySecondary,
      &m_selectedCameraKeySecondary, profileB, false);
  if (!secondaryReady) {
    onLogMessage(
        QString("[Camera] '%1' 연결 설정이 올바르지 않아 B 채널은 중지됩니다.")
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
  bindSelector(m_ui.cameraSecondarySelectorCombo,
               &m_selectedCameraKeySecondary);
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
      m_ui.roiTargetCombo->setCurrentIndex(
          m_roiTarget == RoiTarget::Secondary ? 1 : 0);
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
  onLogMessage(
      QString("[ROI] 편집 대상 변경: %1")
          .arg(m_roiTarget == RoiTarget::Primary ? "카메라 A" : "카메라 B"));
}

bool MainWindowController::refreshCameraConnectionFromConfig(
    CameraManager *cameraManager, const QString &cameraKey,
    QString *resolvedKey, const QString &profileSuffix, bool reloadConfig) {
  if (!cameraManager) {
    return false;
  }

  if (reloadConfig && !Config::instance().load()) {
    onLogMessage("Warning: could not reload config; using existing values.");
  }

  const auto &cfg = Config::instance();
  const QString selectedKey = cameraKey.trimmed().isEmpty()
                                  ? QStringLiteral("camera")
                                  : cameraKey.trimmed();
  CameraConnectionInfo connectionInfo;
  connectionInfo.cameraId = selectedKey;
  connectionInfo.ip = cfg.cameraIp(selectedKey).trimmed();
  connectionInfo.username = cfg.cameraUsername(selectedKey).trimmed();
  connectionInfo.password = cfg.cameraPassword(selectedKey);

  if (!profileSuffix.isEmpty()) {
    connectionInfo.profile = profileSuffix;
  } else {
    connectionInfo.profile = cfg.cameraProfile(selectedKey).trimmed();
    if (connectionInfo.profile.isEmpty()) {
      connectionInfo.profile = QStringLiteral("profile2/media.smp");
    }
  }
  connectionInfo.subProfile = QStringLiteral("profile2/media.smp");

  if (!connectionInfo.isValid()) {
    onLogMessage(QString("[Camera] '%1' 설정이 유효하지 않습니다. (ip/user)")
                     .arg(selectedKey));
    return false;
  }
  cameraManager->setConnectionInfo(connectionInfo);
  if (resolvedKey) {
    *resolvedKey = selectedKey;
  }
  return true;
}

QString MainWindowController::getBestProfileForSize(const QSize &size) const {
  int effectiveWidth = std::min(size.width(), size.height() * 16 / 9);

  if (effectiveWidth >= 3072)
    return QStringLiteral("profile2/media.smp"); // 3840x2160
  if (effectiveWidth >= 2560)
    return QStringLiteral("profile3/media.smp"); // 3072x1728
  if (effectiveWidth >= 1920)
    return QStringLiteral("profile4/media.smp"); // 2560x1440
  if (effectiveWidth >= 1280)
    return QStringLiteral("profile5/media.smp"); // 1920x1080
  if (effectiveWidth >= 640)
    return QStringLiteral("profile6/media.smp"); // 1280x720
  return QStringLiteral("profile7/media.smp");   // 640x360
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
          "[Camera] 듀얼 모드에는 서로 다른 카메라 2개 설정이 필요합니다.");
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
    onLogMessage(QString("[Camera] B 채널 자동 변경: %1")
                     .arg(m_selectedCameraKeySecondary));
  }

  m_viewMode = newMode;
  applyViewModeUiState();
  onLogMessage(QString("[Camera] 뷰 모드 변경: %1")
                   .arg(m_viewMode == ViewMode::Dual ? "2채널 동시" : "1채널"));

  const bool wasRunningPrimary =
      m_cameraManagerPrimary && m_cameraManagerPrimary->isRunning();
  const bool wasRunningSecondary =
      m_cameraManagerSecondary && m_cameraManagerSecondary->isRunning();

  if (wasRunningPrimary || wasRunningSecondary) {
    if (m_viewMode == ViewMode::Single && wasRunningSecondary) {
      m_cameraSessionSecondary.stop();
      m_currentProfileSecondary.clear();
    }
    playCctv();
  } else if (m_viewMode == ViewMode::Single) {
    m_cameraSessionSecondary.stop();
    m_currentProfileSecondary.clear();
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
    onLogMessage("[Camera] A/B 채널은 서로 다른 카메라를 선택해야 합니다.");
    QSignalBlocker blocker(m_ui.cameraPrimarySelectorCombo);
    const int previousIndex =
        m_ui.cameraPrimarySelectorCombo->findData(previousKey);
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
      QString("[Camera] A 채널 변경: %1").arg(m_selectedCameraKeyPrimary));
  reloadRoiForTarget(RoiTarget::Primary);
  refreshZoneTableAllChannels();
  if (m_roiTarget == RoiTarget::Primary) {
    refreshRoiSelectorForTarget();
  }

  if (m_cameraManagerPrimary && m_cameraManagerPrimary->isRunning()) {
    QString profile =
        m_ui.videoWidgetPrimary
            ? getBestProfileForSize(m_ui.videoWidgetPrimary->size())
            : QString();
    m_currentProfilePrimary = profile;
    if (refreshCameraConnectionFromConfig(
            m_cameraManagerPrimary, m_selectedCameraKeyPrimary,
            &m_selectedCameraKeyPrimary, profile)) {
      m_cameraSessionPrimary.playOrRestart();
    }
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
    onLogMessage("[Camera] A/B 채널은 서로 다른 카메라를 선택해야 합니다.");
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
      QString("[Camera] B 채널 변경: %1").arg(m_selectedCameraKeySecondary));
  reloadRoiForTarget(RoiTarget::Secondary);
  refreshZoneTableAllChannels();
  if (m_roiTarget == RoiTarget::Secondary) {
    refreshRoiSelectorForTarget();
  }

  if (m_viewMode != ViewMode::Dual) {
    return;
  }
  if (m_cameraManagerSecondary && m_cameraManagerSecondary->isRunning()) {
    QString profile =
        m_ui.videoWidgetSecondary
            ? getBestProfileForSize(m_ui.videoWidgetSecondary->size())
            : QString();
    m_currentProfileSecondary = profile;
    if (refreshCameraConnectionFromConfig(
            m_cameraManagerSecondary, m_selectedCameraKeySecondary,
            &m_selectedCameraKeySecondary, profile)) {
      m_cameraSessionSecondary.playOrRestart();
    }
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

  // 번호판 인식 로그 필터링 (UI만 무시, qDebug는 출력)
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

void MainWindowController::recordAuditResult(int objectId,
                                             const OcrFullResult &result) {
  if (!m_isBenchmarking || (result.filtered.isEmpty() && result.raw.isEmpty())) {
    return;
  }

  OcrAuditResult audit;
  audit.timestamp =
      QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
  audit.objectId = objectId;
  audit.truth = m_benchmarkTruth;
  audit.raw = result.raw;
  audit.filtered = result.filtered;
  audit.latencyMs = result.latencyMs;
  audit.isRawMatch = (audit.raw == audit.truth);
  audit.isE2EMatch = (audit.filtered == audit.truth);
  audit.cer = calculateCER(audit.truth, audit.filtered);

  m_auditResults.append(audit);
  const QString displayText =
      !result.filtered.isEmpty() ? result.filtered : result.raw;
  onLogMessage(QString("[OCR Audit] %1/%2 수집됨 (ID:%3, Res:%4, Latency:%5ms)")
                   .arg(m_auditResults.size())
                   .arg(m_benchmarkTargetCount)
                   .arg(objectId)
                   .arg(displayText)
                   .arg(result.latencyMs));

  if (m_auditResults.size() >= m_benchmarkTargetCount) {
    onRunBenchmark();
  }
}

void MainWindowController::onOcrResultPrimary(int objectId,
                                              const OcrFullResult &result) {
  if (!m_ui.logView) {
    return;
  }
  const QString displayText =
      !result.filtered.isEmpty() ? result.filtered : result.raw;
  const QString msg = QString("[OCR][A] ID:%1 Result:%2 (Latency:%3ms)")
                          .arg(objectId)
                          .arg(displayText)
                          .arg(result.latencyMs);
  qDebug() << msg;

  // 번호판 인식 로그 필터링
  if (!(m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked())) {
    m_ui.logView->append(msg);
  }

  // OCR 결과를 ParkingService에 전달하여 DB 기록 + 알림 처리
  if (m_parkingServicePrimary && !result.filtered.isEmpty()) {
    m_parkingServicePrimary->processOcrResult(objectId, result.filtered);
  }

  recordAuditResult(objectId, result);
}

void MainWindowController::onOcrResultSecondary(int objectId,
                                                const OcrFullResult &result) {
  if (!m_ui.logView) {
    return;
  }
  const QString displayText =
      !result.filtered.isEmpty() ? result.filtered : result.raw;
  const QString msg = QString("[OCR][B] ID:%1 Result:%2 (Latency:%3ms)")
                          .arg(objectId)
                          .arg(displayText)
                          .arg(result.latencyMs);
  qDebug() << msg;

  if (!(m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked())) {
    m_ui.logView->append(msg);
  }

  if (m_parkingServiceSecondary && !result.filtered.isEmpty()) {
    m_parkingServiceSecondary->processOcrResult(objectId, result.filtered);
  }

  recordAuditResult(objectId, result);
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
  QString nameError;
  if (!targetService->isValidName(typedName, &nameError)) {
    m_ui.logView->append(QString("[ROI] 완료 실패: %1").arg(nameError));
    return;
  }
  if (targetService->isDuplicateName(typedName)) {
    m_ui.logView->append(
        QString("[ROI] 완료 실패: 이름 '%1' 이(가) 이미 존재합니다.")
            .arg(typedName));
    return;
  }

  // 실제 폴리곤 완료는 VideoWidget에서 처리되고,
  // 성공 시 roiPolygonChanged 시그널이 다시 컨트롤러로 올라온다.
  if (!targetWidget->completeRoiDrawing()) {
    m_ui.logView->append("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
  }
}

void MainWindowController::onDeleteSelectedRoi() {
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTarget);
  RoiService *targetService = roiServiceForTarget(m_roiTarget);
  if (!m_ui.roiSelectorCombo || !targetWidget || !targetService ||
      !m_ui.logView) {
    return;
  }
  // 콤보박스에서 현재 선택된 인덱스 확인
  if (m_ui.roiSelectorCombo->currentIndex() < 0) {
    return;
  }
  const int recordIndex = m_ui.roiSelectorCombo->currentData().toInt();
  if (recordIndex < 0 || recordIndex >= targetService->count()) {
    m_ui.logView->append("[ROI] 삭제 실패: ROI를 선택해주세요.");
    return;
  }

  const RoiService::DeleteResult deleteResult =
      targetService->removeAt(recordIndex);
  if (!deleteResult.ok) {
    m_ui.logView->append(
        QString("[ROI][DB] 삭제 실패: %1").arg(deleteResult.error));
    return;
  }
  if (!targetWidget->removeRoiAt(recordIndex)) {
    m_ui.logView->append(
        "[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
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
        QString("[ROI] 삭제 완료: %1").arg(deleteResult.removedName));
  }
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

  RoiService *targetService = roiServiceForTarget(target);
  VideoWidget *targetWidget = videoWidgetForTarget(target);
  if (!targetService || !targetWidget) {
    return;
  }

  const RoiService::CreateResult createResult =
      targetService->createFromPolygon(polygon, frameSize, typedName);
  if (!createResult.ok) {
    // UI에는 이미 방금 그린 ROI가 추가되어 있을 수 있으므로 롤백 처리.
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
  if (targetWidget) {
    const int recordIndex = targetService->count() - 1;
    targetWidget->setRoiLabelAt(
        recordIndex, createResult.record["zone_name"].toString().trimmed());
  }
  appendRoiStructuredLog(createResult.record);
}

void MainWindowController::onMetadataReceivedPrimary(
    const QList<ObjectInfo> &objects) {
  // 메타데이터는 즉시 렌더하지 않고 타임스탬프와 함께 큐에 넣는다.
  // 프레임 도착 시점에 지연값(delay)을 반영해 꺼내 쓰기 위함.
  m_cameraSessionPrimary.pushMetadata(objects,
                                      QDateTime::currentMSecsSinceEpoch());

  // ParkingService에도 메타데이터를 전달하여 입출차 감지 수행
  // 매 프레임마다 ROI 폴리곤을 동기화 (DB 로드/사용자 그리기 반영)
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

  // 1. Stale Frame Drop (실시간성 확보 및 OOM 방지)
  // 큐에 너무 오래 머물러 있던 프레임은 버림 (예: 60ms 이상 지연된 프레임)
  // 버려진 프레임에 대해서는 메모리 할당/복사(Overhead)가 전혀 발생하지 않음
  // (Zero-Copy)
  if ((nowMs - timestampMs) > 60) {
    return;
  }

  // OCR crop은 항상 원본 프레임 기준으로 생성되므로, UI 렌더링 스로틀과
  // 분리해서 RGB 변환 및 메타데이터 동기화를 먼저 수행합니다.
  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);

  const QList<ObjectInfo> readyMetadata =
      m_cameraSessionPrimary.consumeReadyMetadata(nowMs);
  m_ui.videoWidgetPrimary->updateMetadata(readyMetadata);

  // 2. Render Throttling (UI 렌더링 부하 방지)
  // 60fps 수준인 16ms로 제한하여 부하와 부드러움의 타협점 적용
  if (m_renderTimerPrimary.isValid() && m_renderTimerPrimary.elapsed() < 16) {
    return;
  }
  m_renderTimerPrimary.restart();

  // QImage 참조 전달.
  // 내부에서 scaled 되거나 사용(복사)된 후 qimg 스코프 종료 시 framePtr.reset()
  // 실행됨.
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
  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);

  const QList<ObjectInfo> readyMetadata =
      m_cameraSessionSecondary.consumeReadyMetadata(nowMs);
  m_ui.videoWidgetSecondary->updateMetadata(readyMetadata);

  if (m_renderTimerSecondary.isValid() &&
      m_renderTimerSecondary.elapsed() < 16) {
    return;
  }
  m_renderTimerSecondary.restart();

  m_ui.videoWidgetSecondary->updateFrame(qimg);
}

void MainWindowController::onOcrFrameCapturedPrimary(
    QSharedPointer<cv::Mat> framePtr, qint64 timestampMs) {
  if (!m_ui.videoWidgetPrimary || !framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > 120) {
    return;
  }

  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);
  m_ui.videoWidgetPrimary->dispatchOcrRequests(qimg);
}

void MainWindowController::onOcrFrameCapturedSecondary(
    QSharedPointer<cv::Mat> framePtr, qint64 timestampMs) {
  if (!m_ui.videoWidgetSecondary || !framePtr || framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if ((nowMs - timestampMs) > 120) {
    return;
  }

  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);
  m_ui.videoWidgetSecondary->dispatchOcrRequests(qimg);
}

void MainWindowController::onSendEntry() {
  if (!m_ui.entryPlateInput || !m_ui.logView)
    return;

  QString plate = m_ui.entryPlateInput->text().trimmed();
  if (plate.isEmpty()) {
    m_ui.logView->append("[Telegram] 차량번호를 입력해주세요.");
    return;
  }
  m_telegramApi->sendEntryNotice(plate);
}

void MainWindowController::onSendExit() {
  if (!m_ui.exitPlateInput || !m_ui.feeInput || !m_ui.logView)
    return;

  QString plate = m_ui.exitPlateInput->text().trimmed();
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

  // DB 탭의 사용자 테이블도 함께 갱신
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
    qDebug() << msg;

    // 번호판 인식 로그 필터링
    if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
      return;
    }
    m_ui.logView->append(msg);
  }
}

// ── Parking DB Panel Slots ──────────────────────────────────────────

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
        QString("[알림] 🚨 관리자 호출 수신! (User: %1, ChatID: %2)")
            .arg(name, chatId));
  }

  QMessageBox::information(
      nullptr, "관리자 호출",
      QString("🚨 사용자가 관리자를 호출했습니다!\n\n이름: %1\nChat ID: %2")
          .arg(name, chatId));
}

void MainWindowController::onRunBenchmark() {
  if (!m_ui.benchmarkTruthInput || !m_ui.btnRunBenchmark) {
    return;
  }

  if (m_isBenchmarking) {
    m_isBenchmarking = false;
    if (m_ocrCoordinatorPrimary)
      m_ocrCoordinatorPrimary->setStabilizationEnabled(true);
    if (m_ocrCoordinatorSecondary)
      m_ocrCoordinatorSecondary->setStabilizationEnabled(true);
    if (m_ocrCoordinatorPrimary)
      m_ocrCoordinatorPrimary->setEmitPartialResults(false);
    if (m_ocrCoordinatorSecondary)
      m_ocrCoordinatorSecondary->setEmitPartialResults(false);

    m_ui.btnRunBenchmark->setText("실시간 평가 시작");
    m_ui.benchmarkTruthInput->setEnabled(true);
    onLogMessage("[OCR Audit] 사용자에 의해 중단되었습니다.");
    if (!m_auditResults.isEmpty()) {
      generateAuditReport();
    }
    return;
  }

  QString truth = m_ui.benchmarkTruthInput->text().trimmed();
  if (truth.isEmpty()) {
    QMessageBox::warning(nullptr, "오류",
                         "테스트할 정답(번호판)을 입력하세요.");
    return;
  }

  m_benchmarkTruth = truth;
  m_auditResults.clear();
  m_isBenchmarking = true;

  if (m_ocrCoordinatorPrimary)
    m_ocrCoordinatorPrimary->resetRuntimeState();
  if (m_ocrCoordinatorSecondary)
    m_ocrCoordinatorSecondary->resetRuntimeState();
  if (m_ocrCoordinatorPrimary)
    m_ocrCoordinatorPrimary->setStabilizationEnabled(false);
  if (m_ocrCoordinatorSecondary)
    m_ocrCoordinatorSecondary->setStabilizationEnabled(false);
  if (m_ocrCoordinatorPrimary)
    m_ocrCoordinatorPrimary->setEmitPartialResults(true);
  if (m_ocrCoordinatorSecondary)
    m_ocrCoordinatorSecondary->setEmitPartialResults(true);

  m_ui.btnRunBenchmark->setText("실시간 OCR 평가 중단");
  m_ui.benchmarkTruthInput->setEnabled(false);

  onLogMessage(
      QString("[OCR Audit] '%1' 라벨로 실시간 성능 측정 시작 (100회 수집)")
          .arg(truth));
}

void MainWindowController::generateAuditReport() {
  if (m_auditResults.isEmpty())
    return;

  QString fileName = "ocr_live_audit_report_paddle.csv";
  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    onLogMessage("[OCR Audit] 보고서 파일 생성 실패.");
    return;
  }

  QTextStream out(&file);
  out.setGenerateByteOrderMark(true);
  out << "Timestamp,ID,Truth,Raw,Filtered,Latency(ms),RawMatch,E2EMatch,CER\n";

  double totalLatency = 0;
  int rawMatches = 0;
  int e2eMatches = 0;
  int totalCer = 0;
  std::vector<int> latencies;

  for (const auto &res : m_auditResults) {
    out << res.timestamp << "," << res.objectId << "," << res.truth << ","
        << res.raw << "," << res.filtered << "," << res.latencyMs << ","
        << (res.isRawMatch ? "1" : "0") << "," << (res.isE2EMatch ? "1" : "0")
        << "," << res.cer << "\n";

    totalLatency += res.latencyMs;
    latencies.push_back(res.latencyMs);
    if (res.isRawMatch)
      rawMatches++;
    if (res.isE2EMatch)
      e2eMatches++;
    totalCer += res.cer;
  }

  std::sort(latencies.begin(), latencies.end());
  double p50 = latencies[latencies.size() / 2];
  double p95 = latencies[qMin((int)(latencies.size() * 0.95),
                              (int)latencies.size() - 1)];
  double avgLatency = totalLatency / m_auditResults.size();
  double rawAcc = (double)rawMatches / m_auditResults.size() * 100.0;
  double e2eAcc = (double)e2eMatches / m_auditResults.size() * 100.0;
  double avgCer = (double)totalCer / m_auditResults.size();

  out << "\n[OCR Audit Summary]\n";
  out << "Metric,Value\n";
  out << "Sample Count," << m_auditResults.size() << "\n";
  out << "Raw Match Rate," << QString::number(rawAcc, 'f', 1) << " %\n";
  out << "E2E Match Rate," << QString::number(e2eAcc, 'f', 1) << " %\n";
  out << "Average CER," << QString::number(avgCer, 'f', 2) << "\n";
  out << "Avg Latency," << QString::number(avgLatency, 'f', 1) << " ms\n";
  out << "p50 Latency," << QString::number(p50, 'f', 1) << " ms\n";
  out << "p95 Latency," << QString::number(p95, 'f', 1) << " ms\n";

  file.close();

  QString summary = QString("\n[OCR Audit 결과 요약 (PaddleOCR)]\n"
                            "- 샘플 수: %1\n"
                            "- Raw Match: %2%\n"
                            "- E2E Match: %3%\n"
                            "- Avg CER: %4\n"
                            "- Latency (Avg/p50/p95): %5ms / %6ms / %7ms\n"
                            "👉 보고서 저장됨: %8")
                        .arg(m_auditResults.size())
                        .arg(rawAcc, 0, 'f', 1)
                        .arg(e2eAcc, 0, 'f', 1)
                        .arg(avgCer, 0, 'f', 2)
                        .arg(avgLatency, 0, 'f', 1)
                        .arg(p50, 0, 'f', 1)
                        .arg(p95, 0, 'f', 1)
                        .arg(QDir::current().absoluteFilePath(fileName));

  onLogMessage(summary);
  QMessageBox::information(nullptr, "평가 완료", summary);
}

int MainWindowController::calculateCER(const QString &truth,
                                       const QString &pred) {
  int n = truth.length();
  int m = pred.length();
  if (n == 0)
    return m;
  if (m == 0)
    return n;
  QVector<QVector<int>> d(n + 1, QVector<int>(m + 1));
  for (int i = 0; i <= n; ++i)
    d[i][0] = i;
  for (int j = 0; j <= m; ++j)
    d[0][j] = j;
  for (int i = 1; i <= n; ++i) {
    for (int j = 1; j <= m; ++j) {
      int cost = (truth[i - 1] == pred[j - 1]) ? 0 : 1;
      d[i][j] =
          qMin(qMin(d[i - 1][j] + 1, d[i][j - 1] + 1), d[i - 1][j - 1] + cost);
    }
  }
  return d[n][m];
}
