#include "mainwindowcontroller.h"

#include "config/config.h"
#include "database/databasecontext.h"
#include "database/hardwarelogrepository.h"
#include "database/userrepository.h"
#include "database/vehiclerepository.h"
#include "parking/parkingrepository.h"
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
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget> // User Table
#include <algorithm>

MainWindowController::MainWindowController(const UiRefs &uiRefs,
                                           QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
  // 컨트롤러가 하위 서비스의 수명을 소유한다.
  // (QObject parent 관계로 MainWindow 종료 시 함께 정리됨)
  m_cameraManagerPrimary = new CameraManager(this);
  m_cameraManagerSecondary = new CameraManager(this);
  m_ocrCoordinatorPrimary = new PlateOcrCoordinator(this);
  m_ocrCoordinatorSecondary = new PlateOcrCoordinator(this);
  m_telegramApi = new TelegramBotAPI(this);
  m_rpiClient = new RpiTcpClient(this);
  m_rpiClient->setBarrierAngles(90, 0);
  m_parkingServicePrimary = new ParkingService(this);
  m_parkingServiceSecondary = new ParkingService(this);

  // 세션 서비스는 "카메라 제어 + 메타데이터 지연 동기화"를 묶는 파사드 역할.
  m_cameraSessionPrimary.setCameraManager(m_cameraManagerPrimary);
  m_cameraSessionSecondary.setCameraManager(m_cameraManagerSecondary);
  const int delayMs = Config::instance().defaultDelayMs();
  m_cameraSessionPrimary.setDelayMs(delayMs);
  m_cameraSessionSecondary.setDelayMs(delayMs);
  refreshCameraConnectionFromConfig(m_cameraManagerPrimary,
                                    m_selectedCameraKeyPrimary,
                                    &m_selectedCameraKeyPrimary);
  refreshCameraConnectionFromConfig(m_cameraManagerSecondary,
                                    m_selectedCameraKeySecondary,
                                    &m_selectedCameraKeySecondary);

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
  m_rpiClient->init();
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
  initRoiDbForChannels();
  refreshRoiSelectorForTarget();
  refreshZoneTableAllChannels();
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
  if (m_rpiClient) {
    m_rpiClient->disconnectFromServer();
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

  // 백엔드 이벤트(Camera/OCR) -> Controller 슬롯 연결
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

  // RPi UI -> Controller
  if (m_ui.btnRpiConnect) {
    connect(m_ui.btnRpiConnect, &QPushButton::clicked, this,
            &MainWindowController::onRpiConnect);
  }
  if (m_ui.btnRpiDisconnect) {
    connect(m_ui.btnRpiDisconnect, &QPushButton::clicked, this,
            &MainWindowController::onRpiDisconnect);
  }
  if (m_ui.btnBarrierUp) {
    connect(m_ui.btnBarrierUp, &QPushButton::clicked, this,
            &MainWindowController::onRpiBarrierUp);
  }
  if (m_ui.btnBarrierDown) {
    connect(m_ui.btnBarrierDown, &QPushButton::clicked, this,
            &MainWindowController::onRpiBarrierDown);
  }
  if (m_ui.btnLedOn) {
    connect(m_ui.btnLedOn, &QPushButton::clicked, this,
            &MainWindowController::onRpiLedOn);
  }
  if (m_ui.btnLedOff) {
    connect(m_ui.btnLedOff, &QPushButton::clicked, this,
            &MainWindowController::onRpiLedOff);
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

  // RPi Client -> Controller
  connect(m_rpiClient, &RpiTcpClient::connectedChanged, this,
          &MainWindowController::onRpiConnectedChanged);
  connect(m_rpiClient, &RpiTcpClient::parkingStatusUpdated, this,
          &MainWindowController::onRpiParkingStatusUpdated);
  connect(m_rpiClient, &RpiTcpClient::ackReceived, this,
          &MainWindowController::onRpiAckReceived);
  connect(m_rpiClient, &RpiTcpClient::errReceived, this,
          &MainWindowController::onRpiErrReceived);
  connect(m_rpiClient, &RpiTcpClient::logMessage, this,
          &MainWindowController::onRpiLogMessage);

  onRpiConnectedChanged(false);
  onRpiParkingStatusUpdated(false, false, -1, -1);

  // ParkingService -> Controller (로그 이벤트)
  connect(m_parkingServicePrimary, &ParkingService::logMessage, this,
          [this](const QString &msg) {
            onLogMessage(QString("[A] %1").arg(msg));
          });
  connect(m_parkingServiceSecondary, &ParkingService::logMessage, this,
          [this](const QString &msg) {
            onLogMessage(QString("[B] %1").arg(msg));
          });

  // Parking DB Panel
  if (m_ui.btnRefreshLogs) {
    connect(m_ui.btnRefreshLogs, &QPushButton::clicked, this,
            &MainWindowController::onRefreshParkingLogs);
  }
  if (m_ui.btnSearchPlate) {
    connect(m_ui.btnSearchPlate, &QPushButton::clicked, this,
            &MainWindowController::onSearchParkingLogs);
  }
  if (m_ui.btnForcePlate) {
    connect(m_ui.btnForcePlate, &QPushButton::clicked, this,
            &MainWindowController::onForcePlate);
  }
  if (m_ui.btnEditPlate) {
    connect(m_ui.btnEditPlate, &QPushButton::clicked, this,
            &MainWindowController::onEditPlate);
  }

  // New DB CRUD Connections
  if (m_ui.btnRefreshUsers) {
    connect(m_ui.btnRefreshUsers, &QPushButton::clicked, this,
            &MainWindowController::refreshUserTable);
  }
  if (m_ui.btnDeleteUser) {
    connect(m_ui.btnDeleteUser, &QPushButton::clicked, this,
            &MainWindowController::deleteUser);
  }
  if (m_ui.btnRefreshHwLogs) {
    connect(m_ui.btnRefreshHwLogs, &QPushButton::clicked, this,
            &MainWindowController::refreshHwLogs);
  }
  if (m_ui.btnClearHwLogs) {
    connect(m_ui.btnClearHwLogs, &QPushButton::clicked, this,
            &MainWindowController::clearHwLogs);
  }
  if (m_ui.btnRefreshVehicles) {
    connect(m_ui.btnRefreshVehicles, &QPushButton::clicked, this,
            &MainWindowController::refreshVehicleTable);
  }
  if (m_ui.btnDeleteVehicle) {
    connect(m_ui.btnDeleteVehicle, &QPushButton::clicked, this,
            &MainWindowController::deleteVehicle);
  }
  if (m_ui.btnRefreshZone) {
    connect(m_ui.btnRefreshZone, &QPushButton::clicked, this,
            &MainWindowController::refreshZoneTable);
  }

  // 초기 데이터 로드
  onRefreshParkingLogs();
  refreshUserTable();
  refreshHwLogs();
  refreshVehicleTable();
  refreshZoneTableAllChannels();
}

void MainWindowController::refreshZoneTable() { refreshZoneTableAllChannels(); }

void MainWindowController::refreshZoneTableAllChannels() {
  if (!m_ui.zoneTable)
    return;

  m_ui.zoneTable->setRowCount(0);
  auto appendRows = [this](const QVector<QJsonObject> &records) {
    for (const QJsonObject &record : records) {
      int row = m_ui.zoneTable->rowCount();
      m_ui.zoneTable->insertRow(row);

      m_ui.zoneTable->setItem(
          row, 0, new QTableWidgetItem(record["camera_key"].toString()));
      m_ui.zoneTable->setItem(
          row, 1, new QTableWidgetItem(record["rod_id"].toString()));
      m_ui.zoneTable->setItem(
          row, 2, new QTableWidgetItem(record["rod_name"].toString()));

      QString purpose = record["rod_purpose"].toString();
      // 한글 변환 (일반구역/지정구역)
      QString displayPurpose = purpose;
      if (purpose == "General")
        displayPurpose = "일반구역";
      else if (purpose == "Reserved")
        displayPurpose = "지정구역";

      m_ui.zoneTable->setItem(row, 3, new QTableWidgetItem(displayPurpose));
      m_ui.zoneTable->setItem(
          row, 4, new QTableWidgetItem(record["created_at"].toString()));
    }
  };

  appendRows(m_roiServicePrimary.records());
  appendRows(m_roiServiceSecondary.records());

  onLogMessage(QString("주차구역 현황 갱신 완료 (%1건)")
                   .arg(m_roiServicePrimary.count() + m_roiServiceSecondary.count()));
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
    roiLabels.append(record["rod_name"].toString().trimmed());
  }
  widget->queueNormalizedRoiPolygons(initResult.normalizedPolygons, roiLabels);

  if (m_ui.logView && writeLog) {
    m_ui.logView->append(QString("[ROI][DB] %1 채널 '%2' ROI %3개 로드 완료")
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
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

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
                                        &m_selectedCameraKeyPrimary);
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

  const bool secondaryReady =
      refreshCameraConnectionFromConfig(m_cameraManagerSecondary,
                                        m_selectedCameraKeySecondary,
                                        &m_selectedCameraKeySecondary);
  if (!secondaryReady) {
    onLogMessage(QString("[Camera] '%1' 연결 설정이 올바르지 않아 B 채널은 중지됩니다.")
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
  onLogMessage(QString("[ROI] 편집 대상 변경: %1")
                   .arg(m_roiTarget == RoiTarget::Primary ? "카메라 A"
                                                          : "카메라 B"));
}

bool MainWindowController::refreshCameraConnectionFromConfig(
    CameraManager *cameraManager, const QString &cameraKey,
    QString *resolvedKey) {
  if (!cameraManager) {
    return false;
  }

  if (!Config::instance().load()) {
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
                   .arg(m_viewMode == ViewMode::Dual ? "2채널 동시"
                                                     : "1채널"));

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
        "[Camera] A/B 채널은 서로 다른 카메라를 선택해야 합니다.");
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
      QString("[Camera] A 채널 변경: %1").arg(m_selectedCameraKeyPrimary));
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
        "[Camera] A/B 채널은 서로 다른 카메라를 선택해야 합니다.");
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

void MainWindowController::onOcrResultPrimary(int objectId,
                                              const QString &result) {
  if (!m_ui.logView) {
    return;
  }
  const QString msg =
      QString("[OCR][A] ID:%1 Result:%2").arg(objectId).arg(result);
  qDebug() << msg;

  // 번호판 인식 로그 필터링
  if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
    return;
  }
  m_ui.logView->append(msg);

  // OCR 결과를 ParkingService에 전달하여 DB 기록 + 알림 처리
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
    m_ui.logView->append("[ROI] 저장 실패: 프레임 크기가 유효하지 않습니다.");
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
        recordIndex, createResult.record["rod_name"].toString().trimmed());
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

  // === ReID Table Update (ID 기반 누적 관리) ===
  if (m_ui.reidTable && m_roiTarget == RoiTarget::Primary &&
      m_parkingServicePrimary) {
    m_ui.reidTable->setRowCount(0); // Clear

    // 현재 시스템이 추적 중인 모든 객체 목록 가져오기 (실시간 감지 객체 포함)
    const QList<VehicleState> vsList = m_parkingServicePrimary->activeVehicles();

    // ID 순서대로 정렬 (선택 사항, 사용자 편의성)
    QList<VehicleState> sortedVs = vsList;
    std::sort(sortedVs.begin(), sortedVs.end(),
              [](const VehicleState &a, const VehicleState &b) {
                return a.objectId < b.objectId;
              });

    // 현재 시각 가져오기 (Stale 체크용)
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    int staleMs =
        m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;

    for (const VehicleState &vs : sortedVs) {
      if (vs.objectId < 0)
        continue;

      // 현재 프레임에서 감지되었는지 확인 (마지막 감지 시각 기준)
      bool isStale = (nowMs - vs.lastSeenMs) > staleMs;

      // Stale 객체 표시 여부 체크
      if (isStale && m_ui.chkShowStaleObjects &&
          !m_ui.chkShowStaleObjects->isChecked()) {
        continue;
      }

      int row = m_ui.reidTable->rowCount();
      m_ui.reidTable->insertRow(row);

      QColor textColor = isStale ? Qt::gray : Qt::black;

      // Col 0: ID
      auto *idItem = new QTableWidgetItem(QString::number(vs.objectId));
      idItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 0, idItem);

      // Col 1: Type
      auto *typeItem = new QTableWidgetItem(vs.type);
      typeItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 1, typeItem);

      // Col 2: Plate
      auto *plateItem = new QTableWidgetItem(vs.plateNumber);
      plateItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 2, plateItem);

      // Col 3: Score (소수점 2자리)
      auto *scoreItem = new QTableWidgetItem(QString::number(vs.score, 'f', 2));
      scoreItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 3, scoreItem);

      // Col 4: BBox
      const QRectF &rect = vs.boundingBox;
      auto *bboxItem = new QTableWidgetItem(QString("x:%1 y:%2 w:%3 h:%4")
                                                .arg(rect.x(), 0, 'f', 1)
                                                .arg(rect.y(), 0, 'f', 1)
                                                .arg(rect.width(), 0, 'f', 1)
                                                .arg(rect.height(), 0, 'f', 1));
      bboxItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 4, bboxItem);
    }
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
    m_ui.reidTable->setRowCount(0);
    const QList<VehicleState> vsList = m_parkingServiceSecondary->activeVehicles();
    QList<VehicleState> sortedVs = vsList;
    std::sort(sortedVs.begin(), sortedVs.end(),
              [](const VehicleState &a, const VehicleState &b) {
                return a.objectId < b.objectId;
              });

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    int staleMs =
        m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;

    for (const VehicleState &vs : sortedVs) {
      if (vs.objectId < 0)
        continue;

      bool isStale = (nowMs - vs.lastSeenMs) > staleMs;
      if (isStale && m_ui.chkShowStaleObjects &&
          !m_ui.chkShowStaleObjects->isChecked()) {
        continue;
      }

      int row = m_ui.reidTable->rowCount();
      m_ui.reidTable->insertRow(row);

      QColor textColor = isStale ? Qt::gray : Qt::black;
      auto *idItem = new QTableWidgetItem(QString::number(vs.objectId));
      idItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 0, idItem);

      auto *typeItem = new QTableWidgetItem(vs.type);
      typeItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 1, typeItem);

      auto *plateItem = new QTableWidgetItem(vs.plateNumber);
      plateItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 2, plateItem);

      auto *scoreItem = new QTableWidgetItem(QString::number(vs.score, 'f', 2));
      scoreItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 3, scoreItem);

      const QRectF &rect = vs.boundingBox;
      auto *bboxItem = new QTableWidgetItem(QString("x:%1 y:%2 w:%3 h:%4")
                                                .arg(rect.x(), 0, 'f', 1)
                                                .arg(rect.y(), 0, 'f', 1)
                                                .arg(rect.width(), 0, 'f', 1)
                                                .arg(rect.height(), 0, 'f', 1));
      bboxItem->setForeground(textColor);
      m_ui.reidTable->setItem(row, 4, bboxItem);
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

  // 2. Render Throttling (UI 렌더링 부하 방지)
  // 약 30~33fps 수준인 30ms로 제한하여 부하와 부드러움의 타협점 적용
  if (m_renderTimerPrimary.isValid() && m_renderTimerPrimary.elapsed() < 30) {
    return;
  }
  m_renderTimerPrimary.restart();

  // === 렌더링할 프레임에만 BGR → RGB 색상 변환 수행 ===
  // 비디오 스레드에서 모든 프레임에 변환하면 불필요한 CPU 소모가 발생하므로,
  // Throttle/Stale 판정을 통과한 프레임(초당 ~33개)에만 변환을 적용합니다.
  cv::cvtColor(*framePtr, *framePtr, cv::COLOR_BGR2RGB);

  // === Zero-Copy QImage 래핑 ===
  // BGR→RGB 변환 완료 후, OpenCV 버퍼 데이터를 복사하지 않고
  // 쳐다만 보는(shallow copy) QImage 객체를 생성합니다.
  QImage qimg(framePtr->data, framePtr->cols, framePtr->rows, framePtr->step,
              QImage::Format_RGB888);

  // 프레임 갱신 직전에 "현재 시각 기준으로 준비된 메타데이터"만 소비한다.
  // 이렇게 해야 박스/객체 정보와 비디오 프레임이 더 자연스럽게 맞는다.
  m_ui.videoWidgetPrimary->updateMetadata(
      m_cameraSessionPrimary.consumeReadyMetadata(nowMs));

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
  refreshUserTable();
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

static void populateParkingTable(QTableWidget *table,
                                 const QVector<QJsonObject> &logs) {
  if (!table) {
    return;
  }
  table->setRowCount(logs.size());
  for (int i = 0; i < logs.size(); ++i) {
    const QJsonObject &row = logs[i];
    table->setItem(i, 0,
                   new QTableWidgetItem(QString::number(row["id"].toInt())));
    table->setItem(i, 1, new QTableWidgetItem(row["plate_number"].toString()));
    table->setItem(
        i, 2,
        new QTableWidgetItem(QString::number(row["roi_index"].toInt() + 1)));
    table->setItem(i, 3, new QTableWidgetItem(row["entry_time"].toString()));
    table->setItem(i, 4, new QTableWidgetItem(row["exit_time"].toString()));
  }
}

void MainWindowController::onRefreshParkingLogs() {
  ParkingService *service = parkingServiceForTarget(m_roiTarget);
  if (!service) {
    return;
  }
  const QVector<QJsonObject> logs = service->recentLogs(100);
  populateParkingTable(m_ui.parkingLogTable, logs);
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[DB][%1] 전체 새로고침: %2건 표시")
            .arg(service->cameraKey())
            .arg(logs.size()));
  }
}

void MainWindowController::onSearchParkingLogs() {
  if (!m_ui.plateSearchInput) {
    return;
  }
  const QString keyword = m_ui.plateSearchInput->text().trimmed();
  if (keyword.isEmpty()) {
    onRefreshParkingLogs();
    return;
  }
  ParkingService *service = parkingServiceForTarget(m_roiTarget);
  if (!service) {
    return;
  }
  const QVector<QJsonObject> logs = service->searchByPlate(keyword);
  populateParkingTable(m_ui.parkingLogTable, logs);
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[DB][%1] '%2' 검색 결과: %3건")
            .arg(service->cameraKey(), keyword)
            .arg(logs.size()));
  }
}

void MainWindowController::onForcePlate() {
  if (!m_ui.forceObjectIdInput || !m_ui.forceTypeInput ||
      !m_ui.forcePlateInput || !m_ui.forceScoreInput || !m_ui.forceBBoxInput) {
    return;
  }

  const int objectId = m_ui.forceObjectIdInput->value();
  const QString type = m_ui.forceTypeInput->text().trimmed();
  const QString plate = m_ui.forcePlateInput->text().trimmed();
  const double score = m_ui.forceScoreInput->value();
  QString bboxStr = m_ui.forceBBoxInput->text().trimmed();

  // Parse BBox "x y w h"
  // Supports "x:10 y:20..." format or "10 20 100 100"
  // Just simple space parsing
  bboxStr.remove("x:");
  bboxStr.remove("y:");
  bboxStr.remove("w:");
  bboxStr.remove("h:");
  QStringList parts =
      bboxStr.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

  QRectF bbox(0, 0, 0, 0);
  if (parts.size() >= 4) {
    bbox.setX(parts[0].toDouble());
    bbox.setY(parts[1].toDouble());
    bbox.setWidth(parts[2].toDouble());
    bbox.setHeight(parts[3].toDouble());
  }

  ParkingService *service = parkingServiceForTarget(m_roiTarget);
  if (!service) {
    return;
  }
  service->forceObjectData(objectId, type, plate, score, bbox);

  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[DB] 강제 업데이트 요청: ID=%1").arg(objectId));
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

void MainWindowController::onEditPlate() {
  if (!m_ui.parkingLogTable || !m_ui.editPlateInput) {
    return;
  }
  const int currentRow = m_ui.parkingLogTable->currentRow();
  if (currentRow < 0) {
    if (m_ui.logView) {
      m_ui.logView->append("[DB] 수정할 레코드를 먼저 선택해주세요.");
    }
    return;
  }
  const QString newPlate = m_ui.editPlateInput->text().trimmed();
  if (newPlate.isEmpty()) {
    if (m_ui.logView) {
      m_ui.logView->append("[DB] 새 번호판을 입력해주세요.");
    }
    return;
  }
  QTableWidgetItem *idItem = m_ui.parkingLogTable->item(currentRow, 0);
  if (!idItem) {
    return;
  }
  const int recordId = idItem->text().toInt();
  ParkingService *service = parkingServiceForTarget(m_roiTarget);
  if (service && service->updatePlate(recordId, newPlate)) {
    if (m_ui.logView) {
      m_ui.logView->append(QString("[DB] 번호판 수정 완료: ID=%1 → %2")
                               .arg(recordId)
                               .arg(newPlate));
    }
    onRefreshParkingLogs();
  } else {
    if (m_ui.logView) {
      m_ui.logView->append(
          QString("[DB] 번호판 수정 실패: ID=%1").arg(recordId));
    }
  }
}

void MainWindowController::onRpiConnect() {
  if (!m_rpiClient) {
    return;
  }

  const QString host =
      m_ui.rpiHostEdit ? m_ui.rpiHostEdit->text().trimmed() : QString();
  const int port = m_ui.rpiPortSpin ? m_ui.rpiPortSpin->value() : 5000;
  const bool useMock = host.compare("mock", Qt::CaseInsensitive) == 0;

  m_rpiClient->setMockMode(useMock);
  m_rpiClient->setServer(host.isEmpty() ? QStringLiteral("127.0.0.1") : host,
                         static_cast<quint16>(port));
  m_rpiClient->connectToServer();
}

void MainWindowController::onRpiDisconnect() {
  if (m_rpiClient) {
    m_rpiClient->disconnectFromServer();
  }
}

void MainWindowController::onRpiBarrierUp() {
  if (!m_rpiClient || !m_rpiClient->sendBarrierUp()) {
    onRpiLogMessage("[RPI] Barrier up command failed");
  }
}

void MainWindowController::onRpiBarrierDown() {
  if (!m_rpiClient || !m_rpiClient->sendBarrierDown()) {
    onRpiLogMessage("[RPI] Barrier down command failed");
  }
}

void MainWindowController::refreshParkingLogs() { onRefreshParkingLogs(); }

void MainWindowController::deleteParkingLog() {
  if (!m_ui.parkingLogTable)
    return;
  int row = m_ui.parkingLogTable->currentRow();
  if (row < 0)
    return;
  int id = m_ui.parkingLogTable->item(row, 0)->text().toInt();

  ParkingRepository repo;
  QString error;
  if (repo.deleteLog(id, &error)) {
    onLogMessage(QString("[DB] 주차 기록 삭제 완료: ID=%1").arg(id));
    onRefreshParkingLogs();
  } else {
    onLogMessage(QString("[DB] 주차 기록 삭제 실패: %1").arg(error));
  }
}

void MainWindowController::refreshUserTable() {
  if (!m_ui.userDbTable)
    return;
  UserRepository repo;
  QString error;
  QVector<QJsonObject> users = repo.getAllUsersFull(&error);

  m_ui.userDbTable->setRowCount(0);
  for (int i = 0; i < users.size(); ++i) {
    const QJsonObject &u = users[i];
    m_ui.userDbTable->insertRow(i);
    m_ui.userDbTable->setItem(i, 0,
                              new QTableWidgetItem(u["chat_id"].toString()));
    m_ui.userDbTable->setItem(
        i, 1, new QTableWidgetItem(u["plate_number"].toString()));
    m_ui.userDbTable->setItem(i, 2, new QTableWidgetItem(u["name"].toString()));
    m_ui.userDbTable->setItem(i, 3,
                              new QTableWidgetItem(u["phone"].toString()));
    m_ui.userDbTable->setItem(i, 4,
                              new QTableWidgetItem(u["created_at"].toString()));
  }
}

void MainWindowController::deleteUser() {
  if (!m_ui.userDbTable)
    return;
  int row = m_ui.userDbTable->currentRow();
  if (row < 0)
    return;
  QString chatId = m_ui.userDbTable->item(row, 0)->text();

  UserRepository repo;
  QString error;
  if (repo.deleteUser(chatId, &error)) {
    onLogMessage(QString("[DB] 사용자 삭제 완료: ChatID=%1").arg(chatId));
    refreshUserTable();
  } else {
    onLogMessage(QString("[DB] 사용자 삭제 실패: %1").arg(error));
  }
}

void MainWindowController::refreshHwLogs() {
  if (!m_ui.hwLogTable)
    return;
  HardwareLogRepository repo;
  QString error;
  QVector<QJsonObject> logs = repo.getAllLogs(&error);

  m_ui.hwLogTable->setRowCount(0);
  for (int i = 0; i < logs.size(); ++i) {
    const QJsonObject &l = logs[i];
    m_ui.hwLogTable->insertRow(i);
    m_ui.hwLogTable->setItem(
        i, 0, new QTableWidgetItem(QString::number(l["log_id"].toInt())));
    m_ui.hwLogTable->setItem(i, 1,
                             new QTableWidgetItem(l["zone_id"].toString()));
    m_ui.hwLogTable->setItem(i, 2,
                             new QTableWidgetItem(l["device_type"].toString()));
    m_ui.hwLogTable->setItem(i, 3,
                             new QTableWidgetItem(l["action"].toString()));
    m_ui.hwLogTable->setItem(i, 4,
                             new QTableWidgetItem(l["timestamp"].toString()));
  }
}

void MainWindowController::clearHwLogs() {
  HardwareLogRepository repo;
  QString error;
  if (repo.clearLogs(&error)) {
    onLogMessage("[DB] 장치 로그 초기화 완료");
    refreshHwLogs();
  } else {
    onLogMessage(QString("[DB] 장치 로그 초기화 실패: %1").arg(error));
  }
}

void MainWindowController::refreshVehicleTable() {
  if (!m_ui.vehicleTable)
    return;
  VehicleRepository repo;
  QString error;
  QVector<QJsonObject> vehicles = repo.getAllVehicles(&error);

  m_ui.vehicleTable->setRowCount(0);
  for (int i = 0; i < vehicles.size(); ++i) {
    const QJsonObject &v = vehicles[i];
    m_ui.vehicleTable->insertRow(i);
    m_ui.vehicleTable->setItem(
        i, 0, new QTableWidgetItem(v["plate_number"].toString()));
    m_ui.vehicleTable->setItem(i, 1,
                               new QTableWidgetItem(v["car_type"].toString()));
    m_ui.vehicleTable->setItem(i, 2,
                               new QTableWidgetItem(v["car_color"].toString()));
    m_ui.vehicleTable->setItem(
        i, 3, new QTableWidgetItem(v["is_assigned"].toBool() ? "Yes" : "No"));
    m_ui.vehicleTable->setItem(
        i, 4, new QTableWidgetItem(v["updated_at"].toString()));
  }
}

void MainWindowController::deleteVehicle() {
  if (!m_ui.vehicleTable)
    return;
  int row = m_ui.vehicleTable->currentRow();
  if (row < 0)
    return;
  QString plate = m_ui.vehicleTable->item(row, 0)->text();

  VehicleRepository repo;
  QString error;
  if (repo.deleteVehicle(plate, &error)) {
    onLogMessage(QString("[DB] 차량 정보 삭제 완료: %1").arg(plate));
    refreshVehicleTable();
  } else {
    onLogMessage(QString("[DB] 차량 정보 삭제 실패: %1").arg(error));
  }
}

void MainWindowController::onRpiLedOn() {
  if (!m_rpiClient || !m_rpiClient->sendLedOn()) {
    onRpiLogMessage("[RPI] LED on command failed");
  }
}

void MainWindowController::onRpiLedOff() {
  if (!m_rpiClient || !m_rpiClient->sendLedOff()) {
    onRpiLogMessage("[RPI] LED off command failed");
  }
}

void MainWindowController::onRpiConnectedChanged(bool connected) {
  if (m_ui.rpiConnectionStatusLabel) {
    m_ui.rpiConnectionStatusLabel->setText(connected ? "Connected"
                                                     : "Disconnected");
  }
}

void MainWindowController::onRpiParkingStatusUpdated(bool vehicleDetected,
                                                     bool ledOn, int irRaw,
                                                     int servoAngle) {
  if (m_ui.rpiVehicleStatusLabel) {
    m_ui.rpiVehicleStatusLabel->setText(vehicleDetected ? "Detected" : "Clear");
  }
  if (m_ui.rpiLedStatusLabel) {
    m_ui.rpiLedStatusLabel->setText(ledOn ? "ON" : "OFF");
  }
  if (m_ui.rpiIrRawLabel) {
    m_ui.rpiIrRawLabel->setText(irRaw >= 0 ? QString::number(irRaw) : "-");
  }
  if (m_ui.rpiServoAngleLabel) {
    m_ui.rpiServoAngleLabel->setText(
        servoAngle >= 0 ? QString("%1 deg").arg(servoAngle) : "-");
  }
}

void MainWindowController::onRpiAckReceived(const QString &messageId) {
  onRpiLogMessage(QString("[RPI] Ack: %1").arg(messageId));
}

void MainWindowController::onRpiErrReceived(const QString &messageId,
                                            const QString &code,
                                            const QString &message) {
  onRpiLogMessage(QString("[RPI] Error: id=%1 code=%2 message=%3")
                      .arg(messageId, code, message));
}

void MainWindowController::onRpiLogMessage(const QString &message) {
  if (m_ui.logView) {
    m_ui.logView->append(message);
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
