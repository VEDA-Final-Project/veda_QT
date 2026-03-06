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
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRectF>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QStyle>
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
  initChannelCards();
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
    return m_roiServicePrimary.records();
  };
  dbContext.secondaryZoneRecordsProvider = [this]() {
    return m_roiServiceSecondary.records();
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
  if (m_telegramApi) {
    connect(m_telegramApi, &TelegramBotAPI::usersUpdated, m_dbPanelController,
            &DbPanelController::refreshUserTable);
  }

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
    if (m_ui.thumbnailLabels[i]) {
      m_ui.thumbnailLabels[i]->installEventFilter(this);
    }
  }

  initRoiDbForChannels();
  refreshRoiSelectorForTarget();
  updateChannelCardSelection();
  connectSignals();

  m_renderTimerPrimary.start();
  m_renderTimerSecondary.start();
}

void MainWindowController::startInitialCctv() {
  if (m_selectedChannelIndex != -1) {
    return;
  }
  m_primaryVideoReadyNotified = false;
  onChannelCardClicked(0);
}

bool MainWindowController::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::Resize) {
    if (obj == m_ui.videoWidgetPrimary || obj == m_ui.videoWidgetSecondary) {
      m_resizeDebounceTimer->start(150);
    }
  }

  // 채널 카드 클릭 처리
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
  auto reconnectIfProfileChanged =
      [this](int selectedChannelIndex, VideoWidget *videoWidget,
             CameraManager *cameraManager, const QString &cameraKey,
             QString *currentProfile, CameraSessionService &sessionService,
             const QString &targetName) {
        if (selectedChannelIndex == -1 || !videoWidget || !cameraManager ||
            !currentProfile) {
          return;
        }
        if (cameraKey.trimmed().isEmpty() || !cameraManager->isRunning()) {
          return;
        }

        const QString newProfile = getBestProfileForSize(videoWidget->size());
        if (newProfile.isEmpty() || newProfile == *currentProfile) {
          return;
        }

        if (refreshCameraConnectionFromConfig(cameraManager, cameraKey, nullptr,
                                              newProfile, false)) {
          *currentProfile = newProfile;
          videoWidget->setProfileName(newProfile);
          sessionService.playOrRestart();
          onLogMessage(QString("[Camera] %1 리사이즈 재연결: %2")
                           .arg(targetName, newProfile));
        }
      };

  reconnectIfProfileChanged(m_selectedChannelIndex, m_ui.videoWidgetPrimary,
                            m_cameraManagerPrimary, m_selectedCameraKeyPrimary,
                            &m_currentProfilePrimary, m_cameraSessionPrimary,
                            QStringLiteral("Primary"));
  reconnectIfProfileChanged(
      m_secondaryChannelIndex, m_ui.videoWidgetSecondary,
      m_cameraManagerSecondary, m_selectedCameraKeySecondary,
      &m_currentProfileSecondary, m_cameraSessionSecondary,
      QStringLiteral("Secondary"));
}

void MainWindowController::shutdown() {
  for (int i = 0; i < 4; ++i) {
    if (m_thumbSessions[i]) {
      m_thumbSessions[i]->stop();
      delete m_thumbSessions[i];
      m_thumbSessions[i] = nullptr;
    }
    if (m_thumbManagers[i]) {
      m_thumbManagers[i]->stop();
      delete m_thumbManagers[i];
      m_thumbManagers[i] = nullptr;
    }
  }

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
  // 채널 카드 클릭 이벤트 필터 설치
  for (int i = 0; i < 4; ++i) {
    if (m_ui.channelCards[i]) {
      m_ui.channelCards[i]->installEventFilter(this);
    }
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
  const Result<RoiService::RoiInitData> initResult = service->init(cameraKey);
  if (!initResult.isOk()) {
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
  widget->queueNormalizedRoiPolygons(initResult.data.normalizedPolygons,
                                     roiLabels);

  if (m_ui.logView && writeLog) {
    m_ui.logView->append(
        QString("[ROI][DB] %1 채널 '%2' ROI %3개 로드 완료")
            .arg(target == RoiTarget::Primary ? "A" : "B", cameraKey)
            .arg(initResult.data.loadedCount));
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

void MainWindowController::initChannelCards() {
  if (!Config::instance().load()) {
    onLogMessage("Warning: could not reload config; using existing values.");
  }

  QStringList cameraKeys = Config::instance().cameraKeys();
  // 최소 2개 이상의 실제 카메라가 있다고 가정하거나 빈 리스트일 때 기본 처리
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  // 1. 썸네일 스트림 초기화 및 시작 (항상 4개 채널 유지)
  for (int i = 0; i < 4; ++i) {
    if (!m_ui.channelCards[i])
      continue;

    bool isNoSignal = (i >= cameraKeys.size());

    // 항상 보이도록 설정
    m_ui.channelCards[i]->setVisible(true);

    // 이름 설정 (Ch1~Ch4 형식 강제)
    if (m_ui.channelNameLabels[i]) {
      m_ui.channelNameLabels[i]->setText(QString("Ch%1").arg(i + 1));
    }

    if (!isNoSignal) {
      // 실제 카메라가 있는 경우
      m_thumbCameraKeys[i] = cameraKeys[i];

      if (!m_thumbManagers[i]) {
        m_thumbManagers[i] = new CameraManager();
        connect(m_thumbManagers[i], &CameraManager::frameCaptured, this,
                [this, i](QSharedPointer<cv::Mat> frame, qint64 ts) {
                  onFrameCapturedThumb(i, frame, ts);
                });
        m_thumbSessions[i] = new CameraSessionService();
        m_thumbSessions[i]->setCameraManager(m_thumbManagers[i]);
        m_thumbSessions[i]->setDelayMs(500);
      }

      refreshCameraConnectionFromConfig(
          m_thumbManagers[i], m_thumbCameraKeys[i], &m_thumbCameraKeys[i],
          QStringLiteral("profile7/media.smp"), false);
      m_thumbManagers[i]->setTargetFps(5);
      m_thumbManagers[i]->startVideoOnly();

      if (m_ui.channelStatusDots[i]) {
        m_ui.channelStatusDots[i]->setStyleSheet(
            "background: #10b981; border-radius: 5px; border: none;");
      }
    } else {
      // No Signal 채널
      if (m_ui.thumbnailLabels[i]) {
        m_ui.thumbnailLabels[i]->setText("NO SIGNAL");
        m_ui.thumbnailLabels[i]->setStyleSheet(
            "background: #0a0a1a; color: #555; border-radius: 4px; "
            "font-weight: bold; font-size: 10px;");
      }
      if (m_ui.channelStatusDots[i]) {
        m_ui.channelStatusDots[i]->setStyleSheet(
            "background: #ef4444; border-radius: 5px; border: none;");
      }
    }
  }

  // 2. 메인 채널은 자동 시작하지 않음 (사용자 클릭 시 시작)
  m_selectedChannelIndex = -1;
  m_selectedCameraKeyPrimary = QString();
  if (m_ui.videoWidgetPrimary) {
    m_ui.videoWidgetPrimary->setVisible(false);
  }
}

void MainWindowController::updateChannelCardSelection() {
  for (int i = 0; i < 4; ++i) {
    if (m_ui.channelCards[i]) {
      bool isSelected =
          (i == m_selectedChannelIndex || i == m_secondaryChannelIndex);
      m_ui.channelCards[i]->setProperty("selected", isSelected);
      m_ui.channelCards[i]->style()->unpolish(m_ui.channelCards[i]);
      m_ui.channelCards[i]->style()->polish(m_ui.channelCards[i]);
    }
    if (m_ui.channelStatusDots[i]) {
      m_ui.channelStatusDots[i]->setStyleSheet(
          (i == m_selectedChannelIndex || i == m_secondaryChannelIndex)
              ? "background: #00e676; border-radius: 5px; border: none;"
              : "background: #666; border-radius: 5px; border: none;");
    }
  }
}

void MainWindowController::onRoiTargetChanged(int index) {
  const RoiTarget newTarget =
      (index == 1) ? RoiTarget::Secondary : RoiTarget::Primary;
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

  // === OCR 전용 스트림은 설정 파일의 원본 고해상도를 고정 사용 (UI 크기 무관)
  // ===
  connectionInfo.subProfile = cfg.cameraProfile(selectedKey).trimmed();
  if (connectionInfo.subProfile.isEmpty()) {
    connectionInfo.subProfile = QStringLiteral("profile2/media.smp");
  }

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
  // 위젯의 16:9 가로세로비 고려한 유효 너비 계산
  int effectiveWidth = std::min(size.width(), size.height() * 16 / 9);

  // profile2~7 지원 및 1:1 매칭 최적화 (프레임 드랍 방지)
  if (effectiveWidth > 3072)
    return QStringLiteral("profile2/media.smp"); // 4K (3K 초과 시)
  if (effectiveWidth > 2560)
    return QStringLiteral("profile3/media.smp"); // 3K (2.5K 초과 시)
  if (effectiveWidth > 1920)
    return QStringLiteral("profile4/media.smp"); // 2.5K (FHD 초과 시)
  if (effectiveWidth > 1280)
    return QStringLiteral("profile5/media.smp"); // FHD (HD 초과 시)
  if (effectiveWidth > 640)
    return QStringLiteral("profile6/media.smp"); // HD (SD 초과 시)
  return QStringLiteral("profile7/media.smp");   // SD/Thumbnail (640px 이하)
}

void MainWindowController::onChannelCardClicked(int index) {
  if (index < 0 || index >= 4)
    return;

  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  bool isNoSignal = (index >= cameraKeys.size());
  const QString newCameraKey = isNoSignal ? "" : cameraKeys[index];

  // 1. 이미 Primary로 선택된 채널을 클릭한 경우 -> Primary 해제
  if (index == m_selectedChannelIndex) {
    m_selectedChannelIndex = -1;
    m_selectedCameraKeyPrimary = QString();
    m_cameraSessionPrimary.stop();
    if (m_ui.videoWidgetPrimary)
      m_ui.videoWidgetPrimary->setVisible(false);

    updateChannelCardSelection();
    onLogMessage(QString("[Camera] Ch %1 해제").arg(index + 1));

    // 채널 해제 시 썸네일 다시 켜기
    if (index >= 0 && index < 4 && m_thumbManagers[index] && !isNoSignal) {
      m_thumbManagers[index]->startVideoOnly();
    }
    return;
  }

  // 2. 이미 Secondary로 선택된 채널을 클릭한 경우 -> Secondary 해제
  if (index == m_secondaryChannelIndex) {
    m_secondaryChannelIndex = -1;
    m_selectedCameraKeySecondary = QString();
    m_cameraSessionSecondary.stop();
    if (m_ui.videoWidgetSecondary)
      m_ui.videoWidgetSecondary->setVisible(false);

    updateChannelCardSelection();
    onLogMessage(QString("[Camera] 채널 B 해제"));

    // 채널 해제 시 썸네일 다시 켜기
    if (index >= 0 && index < 4 && m_thumbManagers[index]) {
      m_thumbManagers[index]->startVideoOnly();
    }
    return;
  }

  // 3. 새로 클릭한 채널 할당
  // Primary가 비어있으면 먼저 Primary에 할당
  if (m_selectedChannelIndex == -1) {
    m_selectedChannelIndex = index;
    m_selectedCameraKeyPrimary = newCameraKey;

    if (m_parkingServicePrimary) {
      m_parkingServicePrimary->setCameraKey(m_selectedCameraKeyPrimary);
    }
    updateChannelCardSelection();

    if (isNoSignal) {
      onLogMessage(QString("[Camera] Ch %1: 신호 없음").arg(index + 1));
      if (m_ui.videoWidgetPrimary)
        m_ui.videoWidgetPrimary->setVisible(false);
      return;
    }

    onLogMessage(
        QString("[Camera] Ch %1 켜기: %2").arg(index + 1).arg(newCameraKey));
    reloadRoiForTarget(RoiTarget::Primary);
    refreshZoneTableAllChannels();
    refreshRoiSelectorForTarget();

    if (m_ui.videoWidgetPrimary) {
      m_ui.videoWidgetPrimary->setVisible(true);
    }

    if (m_cameraManagerPrimary && !newCameraKey.isEmpty()) {
      QString profile =
          m_ui.videoWidgetPrimary
              ? getBestProfileForSize(m_ui.videoWidgetPrimary->size())
              : QString();
      m_currentProfilePrimary = profile;
      if (m_ui.videoWidgetPrimary) {
        m_ui.videoWidgetPrimary->setProfileName(profile);
      }
      if (refreshCameraConnectionFromConfig(
              m_cameraManagerPrimary, m_selectedCameraKeyPrimary,
              &m_selectedCameraKeyPrimary, profile)) {
        m_cameraSessionPrimary.playOrRestart();

        // 중복 디코딩 방지: 메인에서 켜지면 해당 인덱스의 썸네일 매니저 중지
        if (index >= 0 && index < 4 && m_thumbManagers[index]) {
          m_thumbManagers[index]->stop();
        }
      }
    }
    return;
  }

  // Primary가 차있고 Secondary가 비어있으면 Secondary에 할당
  if (m_secondaryChannelIndex == -1) {
    m_secondaryChannelIndex = index;
    m_selectedCameraKeySecondary = newCameraKey;

    if (m_parkingServiceSecondary) {
      m_parkingServiceSecondary->setCameraKey(m_selectedCameraKeySecondary);
    }
    updateChannelCardSelection();

    if (isNoSignal) {
      onLogMessage(QString("[Camera] Ch %1: 신호 없음").arg(index + 1));
      if (m_ui.videoWidgetSecondary)
        m_ui.videoWidgetSecondary->setVisible(false);
      return;
    }

    onLogMessage(
        QString("[Camera] Ch %1 켜기: %2").arg(index + 1).arg(newCameraKey));
    reloadRoiForTarget(RoiTarget::Secondary);
    refreshZoneTableAllChannels();
    refreshRoiSelectorForTarget();

    if (m_ui.videoWidgetSecondary) {
      m_ui.videoWidgetSecondary->setVisible(true);
    }

    if (m_cameraManagerSecondary && !newCameraKey.isEmpty()) {
      QString profile =
          m_ui.videoWidgetSecondary
              ? getBestProfileForSize(m_ui.videoWidgetSecondary->size())
              : QString();
      m_currentProfileSecondary = profile;
      if (m_ui.videoWidgetSecondary) {
        m_ui.videoWidgetSecondary->setProfileName(profile);
      }
      if (refreshCameraConnectionFromConfig(
              m_cameraManagerSecondary, m_selectedCameraKeySecondary,
              &m_selectedCameraKeySecondary, profile)) {
        m_cameraSessionSecondary.playOrRestart();

        // 중복 디코딩 방지: 메인에서 켜지면 해당 인덱스의 썸네일 매니저 중지
        if (index >= 0 && index < 4 && m_thumbManagers[index]) {
          m_thumbManagers[index]->stop();
        }
      }
    }
    return;
  }

  // 4. 두 채널이 모두 켜져 있는데 세 번째 채널을 누른 경우 -> 무시하거나 경고
  onLogMessage(QString("[Camera] 이미 두 개의 채널이 켜져 있습니다. 시청을 "
                       "원하는 채널을 끄고 다시 시도하세요."));
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

  // 이벤트 로그 패널에도 주요 이벤트 추가
  if (m_ui.eventListWidget) {
    // 중요 이벤트만 표시 ([Camera], [Parking], [OCR], [ROI], [A], [B])
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
      // 최대 100개 유지
      while (m_ui.eventListWidget->count() > 100) {
        delete m_ui.eventListWidget->takeItem(m_ui.eventListWidget->count() -
                                              1);
      }
    }
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
    m_ui.logView->append(QString("[ROI] 삭제 완료: %1").arg(deleteResult.data));
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

  const Result<QJsonObject> createResult =
      targetService->createFromPolygon(polygon, frameSize, typedName);
  if (!createResult.isOk()) {
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
        recordIndex, createResult.data["zone_name"].toString().trimmed());
  }
  appendRoiStructuredLog(createResult.data);
}

void MainWindowController::onMetadataReceivedPrimary(
    const QList<ObjectInfo> &objects) {
  // 메타데이터는 즉시 렌더하지 않고 타임스탬프와 함께 큐에 넣는다.
  // 프레임 도착 시점에 지연값(delay)을 반영해 꺼내 쓰기 위함.
  m_cameraSessionPrimary.pushMetadata(objects,
                                      QDateTime::currentMSecsSinceEpoch());

  // ParkingService에도 메타데이터를 전달하여 입출차 감지 수행
  // ROI 폴리곤 동기화는 1초에 한 번만 수행해도 충분함
  static QElapsedTimer roiSyncTimer;
  if (!roiSyncTimer.isValid() || roiSyncTimer.elapsed() >= 1000) {
    roiSyncTimer.restart();
    if (m_ui.videoWidgetPrimary && m_parkingServicePrimary) {
      const auto intPolygons = m_ui.videoWidgetPrimary->roiPolygons();
      QList<QPolygonF> floatPolygons;
      floatPolygons.reserve(intPolygons.size());
      for (const QPolygon &p : intPolygons)
        floatPolygons.append(QPolygonF(p));
      m_parkingServicePrimary->updateRoiPolygons(floatPolygons);
    }
  }
  const auto &cfg = Config::instance();
  int pruneMs = m_ui.pruneTimeoutInput ? m_ui.pruneTimeoutInput->value() : 5000;
  if (m_parkingServicePrimary) {
    m_parkingServicePrimary->processMetadata(objects, 0, cfg.effectiveWidth(),
                                             cfg.sourceHeight(), pruneMs);
  }

  if (m_ui.reidTable && m_roiTarget == RoiTarget::Primary &&
      m_parkingServicePrimary) {
    // UI 테이블 업데이트 쓰로틀링 (약 3 FPS / 300ms)
    // 매 메타데이터(30fps)마다 테이블을 새로 그리는 것은 엄청난 낭비임
    static QElapsedTimer reidTimer;
    if (!reidTimer.isValid())
      reidTimer.start();

    if (reidTimer.elapsed() >= 300) {
      reidTimer.restart();
      const int staleMs =
          m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;
      const bool showStaleObjects =
          !m_ui.chkShowStaleObjects || m_ui.chkShowStaleObjects->isChecked();
      populateReidTable(m_ui.reidTable,
                        m_parkingServicePrimary->activeVehicles(), staleMs,
                        showStaleObjects);
    }
  }
}

void MainWindowController::onMetadataReceivedSecondary(
    const QList<ObjectInfo> &objects) {
  m_cameraSessionSecondary.pushMetadata(objects,
                                        QDateTime::currentMSecsSinceEpoch());

  static QElapsedTimer roiSyncTimerSec;
  if (!roiSyncTimerSec.isValid() || roiSyncTimerSec.elapsed() >= 1000) {
    roiSyncTimerSec.restart();
    if (m_ui.videoWidgetSecondary && m_parkingServiceSecondary) {
      const auto intPolygons = m_ui.videoWidgetSecondary->roiPolygons();
      QList<QPolygonF> floatPolygons;
      floatPolygons.reserve(intPolygons.size());
      for (const QPolygon &p : intPolygons)
        floatPolygons.append(QPolygonF(p));
      m_parkingServiceSecondary->updateRoiPolygons(floatPolygons);
    }
  }

  const auto &cfg = Config::instance();
  int pruneMs = m_ui.pruneTimeoutInput ? m_ui.pruneTimeoutInput->value() : 5000;
  if (m_parkingServiceSecondary) {
    m_parkingServiceSecondary->processMetadata(objects, 0, cfg.effectiveWidth(),
                                               cfg.sourceHeight(), pruneMs);
  }

  if (m_ui.reidTable && m_roiTarget == RoiTarget::Secondary &&
      m_parkingServiceSecondary) {
    // UI 테이블 업데이트 쓰로틀링 (약 3 FPS / 300ms)
    static QElapsedTimer reidTimerSec;
    if (!reidTimerSec.isValid())
      reidTimerSec.start();

    if (reidTimerSec.elapsed() >= 300) {
      reidTimerSec.restart();
      const int staleMs =
          m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;
      const bool showStaleObjects =
          !m_ui.chkShowStaleObjects || m_ui.chkShowStaleObjects->isChecked();
      populateReidTable(m_ui.reidTable,
                        m_parkingServiceSecondary->activeVehicles(), staleMs,
                        showStaleObjects);
    }
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
  if (m_renderTimerPrimary.isValid() && m_renderTimerPrimary.elapsed() < 33) {
    return;
  }
  m_renderTimerPrimary.restart();

  // QImage 참조 전달.
  // 내부에서 scaled 되거나 사용(복사)된 후 qimg 스코프 종료 시 framePtr.reset()
  // 실행됨.
  m_ui.videoWidgetPrimary->updateFrame(qimg);
  if (!m_primaryVideoReadyNotified) {
    m_primaryVideoReadyNotified = true;
    emit primaryVideoReady();
  }

  // === 썸네일 레이블 재사용 (디코딩 중복 방지 대응) ===
  if (m_selectedChannelIndex >= 0 && m_selectedChannelIndex < 4 &&
      m_ui.thumbnailLabels[m_selectedChannelIndex]) {
    // 썸네일은 약 5 FPS (200ms) 주기로만 업데이트하여 부하 방지
    if (!m_renderTimerThumbs[m_selectedChannelIndex].isValid() ||
        m_renderTimerThumbs[m_selectedChannelIndex].elapsed() >= 200) {
      m_renderTimerThumbs[m_selectedChannelIndex].restart();

      const QSize tSize = m_ui.thumbnailLabels[m_selectedChannelIndex]->size();
      QImage thumbImg =
          qimg.scaled(tSize, Qt::KeepAspectRatio, Qt::FastTransformation);
      QPixmap pix = QPixmap::fromImage(thumbImg.copy());
      QMetaObject::invokeMethod(m_ui.thumbnailLabels[m_selectedChannelIndex],
                                "setPixmap", Qt::QueuedConnection,
                                Q_ARG(QPixmap, pix));
    }
  }
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
      m_renderTimerSecondary.elapsed() < 33) {
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

  // === 성능 최적화: 현재 감지된 객체가 없으면 무거운 4K 변환/디스패치 생략 ===
  if (!m_parkingServicePrimary ||
      m_parkingServicePrimary->activeVehicles().isEmpty()) {
    // ROI 내에 차량이 하나도 없으면 4K 처리가 불필요함
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

  // === 성능 최적화: 현재 감지된 객체가 없으면 무거운 4K 변환/디스패치 생략 ===
  if (!m_parkingServiceSecondary ||
      m_parkingServiceSecondary->activeVehicles().isEmpty()) {
    return;
  }

  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);
  m_ui.videoWidgetSecondary->dispatchOcrRequests(qimg);
}

void MainWindowController::onFrameCapturedThumb(
    int index, QSharedPointer<cv::Mat> framePtr, qint64 timestampMs) {
  if (index < 0 || index >= 4 || !m_ui.thumbnailLabels[index] || !framePtr ||
      framePtr->empty()) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  // 1. 오래된 프레임 무시 (실시간성 유지 및 부하 방지)
  if ((nowMs - timestampMs) > 100) {
    return;
  }

  // 2. 썸네일 업데이트 속도 제한 (약 5 FPS / 200ms 단위)
  if (m_renderTimerThumbs[index].isValid() &&
      m_renderTimerThumbs[index].elapsed() < 200) {
    return;
  }
  m_renderTimerThumbs[index].restart();

  cv::Mat rgbFrame;
  cv::cvtColor(*framePtr, rgbFrame, cv::COLOR_BGR2RGB);

  QImage img(rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step,
             QImage::Format_RGB888);

  const QSize targetSize = m_ui.thumbnailLabels[index]->size();
  QImage scaledImg =
      img.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
  QPixmap pixmap = QPixmap::fromImage(scaledImg.copy());

  // UI 업데이트는 메인 스레드에서 수행해야 함
  QMetaObject::invokeMethod(m_ui.thumbnailLabels[index], "setPixmap",
                            Qt::QueuedConnection, Q_ARG(QPixmap, pixmap));
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

  // 논블로킹 팝업: OK 누르기 전에도 다음 호출 수신 가능
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
