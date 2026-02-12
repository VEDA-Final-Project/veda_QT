#include "mainwindowcontroller.h"

#include "config/config.h"
#include "database/databasecontext.h"
#include "ui/video/videowidget.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QRectF>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget> // User Table

MainWindowController::MainWindowController(const UiRefs &uiRefs,
                                           QObject *parent)
    : QObject(parent), m_ui(uiRefs) {
  // 컨트롤러가 하위 서비스의 수명을 소유한다.
  // (QObject parent 관계로 MainWindow 종료 시 함께 정리됨)
  m_cameraManager = new CameraManager(this);
  m_ocrCoordinator = new PlateOcrCoordinator(this);
  m_telegramApi = new TelegramBotAPI(this);
  m_rpiClient = new RpiTcpClient(this);
  m_rpiClient->setBarrierAngles(90, 0);
  m_parkingService = new ParkingService(this);

  // 세션 서비스는 "카메라 제어 + 메타데이터 지연 동기화"를 묶는 파사드 역할.
  m_cameraSession.setCameraManager(m_cameraManager);
  m_cameraSession.setDelayMs(Config::instance().defaultDelayMs());

  // 통합 DB 초기화 (veda.db)
  const QString dbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/veda.db");
  DatabaseContext::init(dbPath);

  // Parking 서비스 초기화 (DB Context 사용)
  QString parkingError;
  if (!m_parkingService->init(&parkingError)) {
    qWarning() << "[Parking] Service init failed:" << parkingError;
  }
  m_parkingService->setTelegramApi(m_telegramApi);

  // ROI DB 로드 -> UI 반영 -> 시그널 연결 순으로 초기화.
  initRoiDb();
  connectSignals();
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
  m_cameraSession.stop();
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
  if (m_ui.videoWidget) {
    connect(m_ui.videoWidget, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(m_ui.videoWidget, &VideoWidget::roiPolygonChanged, this,
            &MainWindowController::onRoiPolygonChanged);
    connect(m_ui.videoWidget, &VideoWidget::ocrRequested, m_ocrCoordinator,
            &PlateOcrCoordinator::requestOcr);
  }

  // 백엔드 이벤트(Camera/OCR) -> Controller 슬롯 연결
  connect(m_cameraManager, &CameraManager::metadataReceived, this,
          &MainWindowController::onMetadataReceived);
  connect(m_cameraManager, &CameraManager::frameCaptured, this,
          &MainWindowController::onFrameCaptured);
  connect(m_cameraManager, &CameraManager::logMessage, this,
          &MainWindowController::onLogMessage);
  connect(m_ocrCoordinator, &PlateOcrCoordinator::ocrReady, this,
          &MainWindowController::onOcrResult);

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
  connect(m_parkingService, &ParkingService::logMessage, this,
          &MainWindowController::onLogMessage);

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

  // 초기 데이터 로드
  onRefreshParkingLogs();
}

void MainWindowController::initRoiDb() {
  // 통합 DB 사용 (인자 없음)
  const RoiService::InitResult initResult = m_roiService.init();
  if (!initResult.ok) {
    if (m_ui.logView) {
      m_ui.logView->append(
          QString("[ROI][DB] 초기화 실패: %1").arg(initResult.error));
    }
    return;
  }

  if (m_ui.videoWidget) {
    // DB에는 정규화 좌표(0~1)로 저장되어 있으므로
    // 첫 프레임 렌더 시 실제 픽셀 좌표로 복원하도록 큐에 적재한다.
    QStringList roiLabels;
    const QVector<QJsonObject> &records = m_roiService.records();
    roiLabels.reserve(records.size());
    for (const QJsonObject &record : records) {
      roiLabels.append(record["rod_name"].toString().trimmed());
    }
    m_ui.videoWidget->queueNormalizedRoiPolygons(initResult.normalizedPolygons,
                                                 roiLabels);
  }

  refreshRoiSelector();
  if (m_ui.logView && initResult.loadedCount > 0) {
    m_ui.logView->append(
        QString("[ROI][DB] %1개 ROI 로드 완료").arg(initResult.loadedCount));
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
  if (!m_ui.roiSelectorCombo) {
    return;
  }
  m_ui.roiSelectorCombo->clear();
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

  const QVector<QJsonObject> &records = m_roiService.records();
  for (int i = 0; i < records.size(); ++i) {
    const QJsonObject &record = records[i];
    const QString name =
        record["rod_name"].toString(QString("rod_%1").arg(i + 1));
    const QString purpose = record["rod_purpose"].toString();
    m_ui.roiSelectorCombo->addItem(QString("%1 | %2").arg(name, purpose), i);
  }
}

void MainWindowController::playCctv() {
  // 실행 중이면 재시작, 아니면 시작 (토글이 아닌 "재생/재연결" 동작)
  m_cameraSession.playOrRestart();
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

void MainWindowController::onOcrResult(int objectId, const QString &result) {
  if (!m_ui.logView) {
    return;
  }
  const QString msg =
      QString("[OCR] ID:%1 Result:%2").arg(objectId).arg(result);
  qDebug() << msg;

  // 번호판 인식 로그 필터링
  if (m_ui.chkShowPlateLogs && !m_ui.chkShowPlateLogs->isChecked()) {
    return;
  }
  m_ui.logView->append(msg);

  // OCR 결과를 ParkingService에 전달하여 DB 기록 + 알림 처리
  m_parkingService->processOcrResult(objectId, result);
}

void MainWindowController::onStartRoiDraw() {
  if (!m_ui.videoWidget || !m_ui.logView) {
    return;
  }
  m_ui.videoWidget->startRoiDrawing();
  m_ui.logView->append(
      "[ROI] Draw mode: left-click points, then press 'ROI 완료'.");
}

void MainWindowController::onCompleteRoiDraw() {
  if (!m_ui.videoWidget || !m_ui.logView) {
    return;
  }
  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  QString nameError;
  if (!m_roiService.isValidName(typedName, &nameError)) {
    m_ui.logView->append(QString("[ROI] 완료 실패: %1").arg(nameError));
    return;
  }
  if (m_roiService.isDuplicateName(typedName)) {
    m_ui.logView->append(
        QString("[ROI] 완료 실패: 이름 '%1' 이(가) 이미 존재합니다.")
            .arg(typedName));
    return;
  }

  // 실제 폴리곤 완료는 VideoWidget에서 처리되고,
  // 성공 시 roiPolygonChanged 시그널이 다시 컨트롤러로 올라온다.
  if (!m_ui.videoWidget->completeRoiDrawing()) {
    m_ui.logView->append("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
  }
}

void MainWindowController::onDeleteSelectedRoi() {
  if (!m_ui.roiSelectorCombo || !m_ui.videoWidget || !m_ui.logView) {
    return;
  }
  // 콤보박스에서 현재 선택된 인덱스 확인
  int idx = m_ui.roiSelectorCombo->currentIndex();
  if (idx < 0) {
    return;
  }
  const int recordIndex = m_ui.roiSelectorCombo->currentData().toInt();
  if (recordIndex < 0 || recordIndex >= m_roiService.count()) {
    m_ui.logView->append("[ROI] 삭제 실패: ROI를 선택해주세요.");
    return;
  }

  const RoiService::DeleteResult deleteResult =
      m_roiService.removeAt(recordIndex);
  if (!deleteResult.ok) {
    m_ui.logView->append(
        QString("[ROI][DB] 삭제 실패: %1").arg(deleteResult.error));
    return;
  }
  if (!m_ui.videoWidget->removeRoiAt(recordIndex)) {
    m_ui.logView->append(
        "[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
    return;
  }

  refreshRoiSelector();
  int nextRecordIndex = recordIndex;
  if (nextRecordIndex >= m_roiService.count()) {
    nextRecordIndex = m_roiService.count() - 1;
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
  m_ui.logView->append(QString("[ROI] bbox x:%1 y:%2 w:%3 h:%4")
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

  const RoiService::CreateResult createResult =
      m_roiService.createFromPolygon(polygon, frameSize, typedName, purpose);
  if (!createResult.ok) {
    // UI에는 이미 방금 그린 ROI가 추가되어 있을 수 있으므로 롤백 처리.
    if (m_ui.videoWidget && m_ui.videoWidget->roiCount() > 0) {
      m_ui.videoWidget->removeRoiAt(m_ui.videoWidget->roiCount() - 1);
    }
    m_ui.logView->append(
        QString("[ROI][DB] 저장 실패: %1").arg(createResult.error));
    refreshRoiSelector();
    return;
  }

  refreshRoiSelector();
  if (m_ui.roiSelectorCombo) {
    m_ui.roiSelectorCombo->setCurrentIndex(m_ui.roiSelectorCombo->count() - 1);
  }
  if (m_ui.videoWidget) {
    const int recordIndex = m_roiService.count() - 1;
    m_ui.videoWidget->setRoiLabelAt(
        recordIndex, createResult.record["rod_name"].toString().trimmed());
  }
  appendRoiStructuredLog(createResult.record);
}

void MainWindowController::onMetadataReceived(
    const QList<ObjectInfo> &objects) {
  // 메타데이터는 즉시 렌더하지 않고 타임스탬프와 함께 큐에 넣는다.
  // 프레임 도착 시점에 지연값(delay)을 반영해 꺼내 쓰기 위함.
  m_cameraSession.pushMetadata(objects, QDateTime::currentMSecsSinceEpoch());

  // ParkingService에도 메타데이터를 전달하여 입출차 감지 수행
  // 매 프레임마다 ROI 폴리곤을 동기화 (DB 로드/사용자 그리기 반영)
  if (m_ui.videoWidget) {
    m_parkingService->updateRoiPolygons(m_ui.videoWidget->roiPolygons());
  }
  const auto &cfg = Config::instance();
  int pruneMs = m_ui.pruneTimeoutInput ? m_ui.pruneTimeoutInput->value() : 5000;
  m_parkingService->processMetadata(objects, cfg.effectiveWidth(),
                                    cfg.sourceHeight(), pruneMs);

  // === ReID Table Update (ID 기반 누적 관리) ===
  if (m_ui.reidTable) {
    m_ui.reidTable->setRowCount(0); // Clear

    // 현재 시스템이 추적 중인 모든 객체 목록 가져오기 (실시간 감지 객체 포함)
    const QList<VehicleState> vsList = m_parkingService->activeVehicles();

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

void MainWindowController::onFrameCaptured(const QImage &frame) {
  if (!m_ui.videoWidget) {
    return;
  }

  // 프레임 갱신 직전에 "현재 시각 기준으로 준비된 메타데이터"만 소비한다.
  // 이렇게 해야 박스/객체 정보와 비디오 프레임이 더 자연스럽게 맞는다.
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_ui.videoWidget->updateMetadata(m_cameraSession.consumeReadyMetadata(nowMs));
  m_ui.videoWidget->updateFrame(frame);
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
  const QVector<QJsonObject> logs = m_parkingService->recentLogs(100);
  populateParkingTable(m_ui.parkingLogTable, logs);
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[DB] 전체 새로고침: %1건 표시").arg(logs.size()));
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
  const QVector<QJsonObject> logs = m_parkingService->searchByPlate(keyword);
  populateParkingTable(m_ui.parkingLogTable, logs);
  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[DB] '%1' 검색 결과: %2건").arg(keyword).arg(logs.size()));
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

  m_parkingService->forceObjectData(objectId, type, plate, score, bbox);

  if (m_ui.logView) {
    m_ui.logView->append(
        QString("[DB] 강제 업데이트 요청: ID=%1").arg(objectId));
  }
}

void MainWindowController::onReidTableCellClicked(int row, int column) {
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
  if (m_parkingService->updatePlate(recordId, newPlate)) {
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
