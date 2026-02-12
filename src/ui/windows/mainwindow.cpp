#include "mainwindow.h"
#include "mainwindowcontroller.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  // UI 레이아웃 및 위젯 생성
  setupUi();

  // 종료 버튼이 존재하면 클릭 시 창 닫기
  if (m_btnExit) {
    connect(m_btnExit, &QPushButton::clicked, this, &MainWindow::close);
  }

  /*
   * Controller에 전달할 UI 참조 묶음
   */
  MainWindowController::UiRefs uiRefs;
  uiRefs.videoWidget = m_videoWidget;
  uiRefs.roiNameEdit = m_roiNameEdit;
  uiRefs.roiPurposeCombo = m_roiPurposeCombo;
  uiRefs.roiSelectorCombo = m_roiSelectorCombo;
  uiRefs.logView = m_logView;
  uiRefs.btnPlay = m_btnPlay;
  uiRefs.btnApplyRoi = m_btnApplyRoi;
  uiRefs.btnFinishRoi = m_btnFinishRoi;
  uiRefs.btnDeleteRoi = m_btnDeleteRoi;

  // Telegram Widgets
  uiRefs.userCountLabel = m_userCountLabel;
  uiRefs.entryPlateInput = m_entryPlateInput;
  uiRefs.btnSendEntry = m_btnSendEntry;
  uiRefs.exitPlateInput = m_exitPlateInput;
  uiRefs.feeInput = m_feeInput;
  uiRefs.btnSendExit = m_btnSendExit;
  uiRefs.userTable = m_userTable;

  // RPi Widgets
  uiRefs.rpiHostEdit = m_rpiHostEdit;
  uiRefs.rpiPortSpin = m_rpiPortSpin;
  uiRefs.btnRpiConnect = m_btnRpiConnect;
  uiRefs.btnRpiDisconnect = m_btnRpiDisconnect;
  uiRefs.btnBarrierUp = m_btnBarrierUp;
  uiRefs.btnBarrierDown = m_btnBarrierDown;
  uiRefs.btnLedOn = m_btnLedOn;
  uiRefs.btnLedOff = m_btnLedOff;
  uiRefs.rpiConnectionStatusLabel = m_rpiConnectionStatusLabel;
  uiRefs.rpiVehicleStatusLabel = m_rpiVehicleStatusLabel;
  uiRefs.rpiLedStatusLabel = m_rpiLedStatusLabel;
  uiRefs.rpiIrRawLabel = m_rpiIrRawLabel;
  uiRefs.rpiServoAngleLabel = m_rpiServoAngleLabel;

  // Parking DB Panel Widgets
  uiRefs.parkingLogTable = m_parkingLogTable;
  uiRefs.plateSearchInput = m_plateSearchInput;
  uiRefs.btnSearchPlate = m_btnSearchPlate;
  uiRefs.btnRefreshLogs = m_btnRefreshLogs;
  uiRefs.forcePlateInput = m_forcePlateInput;
  uiRefs.forceObjectIdInput = m_forceObjectIdInput;
  uiRefs.btnForcePlate = m_btnForcePlate;
  uiRefs.editPlateInput = m_editPlateInput;
  uiRefs.btnEditPlate = m_btnEditPlate;

  // New DB sub-tab refs
  uiRefs.userDbTable = m_userDbTable;
  uiRefs.btnRefreshUsers = m_btnRefreshUsers;
  uiRefs.btnDeleteUser = m_btnDeleteUser;

  uiRefs.hwLogTable = m_hwLogTable;
  uiRefs.btnRefreshHwLogs = m_btnRefreshHwLogs;
  uiRefs.btnClearHwLogs = m_btnClearHwLogs;

  uiRefs.vehicleTable = m_vehicleTable;
  uiRefs.btnRefreshVehicles = m_btnRefreshVehicles;
  uiRefs.btnDeleteVehicle = m_btnDeleteVehicle;

  uiRefs.zoneTable = m_zoneTable;
  uiRefs.btnRefreshZone = m_btnRefreshZone;

  // Log Filter
  uiRefs.chkShowPlateLogs = m_chkShowPlateLogs;
  // ReID Table
  uiRefs.reidTable = m_reidTable;
  uiRefs.forceTypeInput = m_forceTypeInput;
  uiRefs.forceScoreInput = m_forceScoreInput;
  uiRefs.forceBBoxInput = m_forceBBoxInput;
  uiRefs.staleTimeoutInput = m_staleTimeoutInput;
  uiRefs.pruneTimeoutInput = m_pruneTimeoutInput;
  uiRefs.chkShowStaleObjects = m_chkShowStaleObjects;

  // Controller 생성 (MainWindow가 부모 → 자동 메모리 관리)
  m_controller = new MainWindowController(uiRefs, this);

  // === Signal/Slot Connections ===
  if (m_btnPlay) {
    connect(m_btnPlay, &QPushButton::clicked, m_controller,
            &MainWindowController::playCctv);
  }
  if (m_btnApplyRoi) {
    connect(m_btnApplyRoi, &QPushButton::clicked, m_controller,
            &MainWindowController::onStartRoiDraw);
  }
  if (m_btnFinishRoi) {
    connect(m_btnFinishRoi, &QPushButton::clicked, m_controller,
            &MainWindowController::onCompleteRoiDraw);
  }
  if (m_btnDeleteRoi) {
    connect(m_btnDeleteRoi, &QPushButton::clicked, m_controller,
            &MainWindowController::onDeleteSelectedRoi);
  }
  // Telegram
  if (m_btnSendEntry) {
    connect(m_btnSendEntry, &QPushButton::clicked, m_controller,
            &MainWindowController::onSendEntry);
  }
  if (m_btnSendExit) {
    connect(m_btnSendExit, &QPushButton::clicked, m_controller,
            &MainWindowController::onSendExit);
  }
  // Parking DB
  if (m_btnSearchPlate) {
    connect(m_btnSearchPlate, &QPushButton::clicked, m_controller,
            &MainWindowController::onSearchParkingLogs);
  }
  if (m_btnRefreshLogs) {
    connect(m_btnRefreshLogs, &QPushButton::clicked, m_controller,
            &MainWindowController::onRefreshParkingLogs);
  }
  if (m_btnForcePlate) {
    connect(m_btnForcePlate, &QPushButton::clicked, m_controller,
            &MainWindowController::onForcePlate);
  }
  if (m_btnEditPlate) {
    connect(m_btnEditPlate, &QPushButton::clicked, m_controller,
            &MainWindowController::onEditPlate);
  }
  if (m_reidTable) {
    connect(m_reidTable, &QTableWidget::cellClicked, m_controller,
            &MainWindowController::onReidTableCellClicked);
  }

  // 초기 창 크기 설정
  resize(1000, 700);
}

/*
 * 창이 닫힐 때 호출되는 이벤트
 * - Controller의 종료 로직(shutdown) 수행
 * - 스레드 / RTSP / 리소스 정리 목적
 */
void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_controller) {
    m_controller->shutdown();
  }
  QMainWindow::closeEvent(event);
}

void MainWindow::setupUi() {
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QVBoxLayout *layout = new QVBoxLayout(centralWidget);

  QTabWidget *tabWidget = new QTabWidget(this);

  // ======================
  // Tab 1: CCTV 보기
  // ======================
  QWidget *cctvTab = new QWidget(this);
  QVBoxLayout *cctvLayout = new QVBoxLayout(cctvTab);

  // 상단 버튼 / 입력 영역
  QHBoxLayout *btnLayout = new QHBoxLayout();
  m_btnPlay = new QPushButton("CCTV 보기", this);
  m_btnExit = new QPushButton("종료", this);
  m_btnApplyRoi = new QPushButton("주차 구역 설정하기", this);
  m_btnFinishRoi = new QPushButton("ROI 완료", this);

  QLabel *nameLabel = new QLabel("이름:", this);
  m_roiNameEdit = new QLineEdit(this);
  m_roiNameEdit->setPlaceholderText(QStringLiteral("ROI 이름 입력(필수)"));
  m_roiNameEdit->setClearButtonEnabled(true);
  m_roiNameEdit->setMaxLength(20);
  m_roiNameEdit->setMinimumWidth(180);
  m_roiNameEdit->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("^[A-Za-z0-9가-힣 _-]{0,20}$")),
      m_roiNameEdit));

  QLabel *purposeLabel = new QLabel("목적:", this);
  m_roiPurposeCombo = new QComboBox(this);
  m_roiPurposeCombo->addItem(QStringLiteral("지정 주차"));
  m_roiPurposeCombo->addItem(QStringLiteral("일반 주차"));
  m_roiPurposeCombo->setMinimumWidth(120);

  QLabel *roiLabel = new QLabel("ROI:", this);
  m_roiSelectorCombo = new QComboBox(this);
  m_roiSelectorCombo->setMinimumContentsLength(24);
  m_roiSelectorCombo->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_roiSelectorCombo->setMinimumWidth(260);
  m_roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

  m_btnDeleteRoi = new QPushButton("ROI 삭제", this);

  btnLayout->addWidget(m_btnPlay);
  btnLayout->addWidget(m_btnExit);
  btnLayout->addSpacing(20);
  btnLayout->addWidget(m_btnApplyRoi);
  btnLayout->addWidget(m_btnFinishRoi);
  btnLayout->addSpacing(20);
  btnLayout->addWidget(nameLabel);
  btnLayout->addWidget(m_roiNameEdit);
  btnLayout->addSpacing(12);
  btnLayout->addWidget(purposeLabel);
  btnLayout->addWidget(m_roiPurposeCombo);
  btnLayout->addSpacing(12);
  btnLayout->addWidget(roiLabel);
  btnLayout->addWidget(m_roiSelectorCombo);
  btnLayout->addWidget(m_btnDeleteRoi);

  // 비디오 위젯
  m_videoWidget = new VideoWidget(this);

  cctvLayout->addLayout(btnLayout);
  cctvLayout->addWidget(m_videoWidget);

  // ======================
  // Tab 2: 텔레그램 테스트
  // ======================
  QWidget *telegramTab = new QWidget(this);
  QVBoxLayout *tgLayout = new QVBoxLayout(telegramTab);

  // 1. 상태 정보
  QGroupBox *statusGroup = new QGroupBox(QString::fromUtf8("상태 정보"), this);
  QFormLayout *statusForm = new QFormLayout();
  m_userCountLabel = new QLabel(QString::fromUtf8("- 명"), this);
  statusForm->addRow(QString::fromUtf8("등록된 사용자 수:"), m_userCountLabel);
  statusGroup->setLayout(statusForm);
  tgLayout->addWidget(statusGroup);

  // 2. 입차 알림 테스트
  QGroupBox *entryGroup =
      new QGroupBox(QString::fromUtf8("입차 알림 테스트"), this);
  QHBoxLayout *entryRow = new QHBoxLayout();
  m_entryPlateInput = new QLineEdit(this);
  m_entryPlateInput->setPlaceholderText(
      QString::fromUtf8("차량번호 입력 (예: 123가4567)"));
  m_btnSendEntry = new QPushButton(QString::fromUtf8("입차 알림 전송"), this);
  entryRow->addWidget(new QLabel(QString::fromUtf8("차량번호:"), this));
  entryRow->addWidget(m_entryPlateInput);
  entryRow->addWidget(m_btnSendEntry);
  entryGroup->setLayout(entryRow);
  tgLayout->addWidget(entryGroup);

  // 3. 출차 알림 테스트
  QGroupBox *exitGroup =
      new QGroupBox(QString::fromUtf8("출차 알림 테스트"), this);
  QHBoxLayout *exitRow = new QHBoxLayout();
  m_exitPlateInput = new QLineEdit(this);
  m_exitPlateInput->setPlaceholderText(
      QString::fromUtf8("차량번호 입력 (예: 123가4567)"));
  m_feeInput = new QSpinBox(this);
  m_feeInput->setRange(0, 999999);
  m_feeInput->setValue(5000);
  m_feeInput->setSuffix(QString::fromUtf8(" 원"));
  m_feeInput->setSingleStep(1000);

  m_btnSendExit = new QPushButton(QString::fromUtf8("출차 알림 전송"), this);

  exitRow->addWidget(new QLabel(QString::fromUtf8("차량번호:"), this));
  exitRow->addWidget(m_exitPlateInput);
  exitRow->addWidget(new QLabel(QString::fromUtf8("요금:"), this));
  exitRow->addWidget(m_feeInput);
  exitRow->addWidget(m_btnSendExit);
  exitGroup->setLayout(exitRow);
  tgLayout->addWidget(exitGroup);

  // 4. 등록된 사용자 목록
  QGroupBox *userListGroup =
      new QGroupBox(QString::fromUtf8("등록된 사용자 목록"), this);
  QVBoxLayout *userListLayout = new QVBoxLayout();
  m_userTable = new QTableWidget(this);
  m_userTable->setColumnCount(2);
  m_userTable->setHorizontalHeaderLabels(
      QStringList() << "Chat ID" << QString::fromUtf8("차량번호"));
  m_userTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  userListLayout->addWidget(m_userTable);
  userListGroup->setLayout(userListLayout);
  tgLayout->addWidget(userListGroup);

  tgLayout->addStretch();

  // ======================
  // Tab 3: RPi 제어
  // ======================
  QWidget *rpiTab = new QWidget(this);
  QVBoxLayout *rpiLayout = new QVBoxLayout(rpiTab);

  // 1. 연결 설정
  QGroupBox *rpiConnGroup =
      new QGroupBox(QString::fromUtf8("RPi 연결 설정"), this);
  QHBoxLayout *rpiConnRow = new QHBoxLayout();
  m_rpiHostEdit = new QLineEdit(this);
  m_rpiHostEdit->setPlaceholderText(
      QStringLiteral("RPi Host (예: 192.168.0.50)"));
  m_rpiHostEdit->setText(QStringLiteral("127.0.0.1"));
  m_rpiPortSpin = new QSpinBox(this);
  m_rpiPortSpin->setRange(1, 65535);
  m_rpiPortSpin->setValue(5000);
  m_btnRpiConnect = new QPushButton(QString::fromUtf8("연결"), this);
  m_btnRpiDisconnect = new QPushButton(QString::fromUtf8("해제"), this);
  rpiConnRow->addWidget(new QLabel(QString::fromUtf8("Host:"), this));
  rpiConnRow->addWidget(m_rpiHostEdit);
  rpiConnRow->addWidget(new QLabel(QString::fromUtf8("Port:"), this));
  rpiConnRow->addWidget(m_rpiPortSpin);
  rpiConnRow->addWidget(m_btnRpiConnect);
  rpiConnRow->addWidget(m_btnRpiDisconnect);
  rpiConnGroup->setLayout(rpiConnRow);
  rpiLayout->addWidget(rpiConnGroup);

  // 2. 제어
  QGroupBox *rpiControlGroup =
      new QGroupBox(QString::fromUtf8("차단봉 / LED 제어"), this);
  QHBoxLayout *rpiControlRow = new QHBoxLayout();
  m_btnBarrierUp = new QPushButton(QString::fromUtf8("차단봉 올리기"), this);
  m_btnBarrierDown = new QPushButton(QString::fromUtf8("차단봉 내리기"), this);
  m_btnLedOn = new QPushButton(QString::fromUtf8("LED ON"), this);
  m_btnLedOff = new QPushButton(QString::fromUtf8("LED OFF"), this);
  rpiControlRow->addWidget(m_btnBarrierUp);
  rpiControlRow->addWidget(m_btnBarrierDown);
  rpiControlRow->addSpacing(20);
  rpiControlRow->addWidget(m_btnLedOn);
  rpiControlRow->addWidget(m_btnLedOff);
  rpiControlGroup->setLayout(rpiControlRow);
  rpiLayout->addWidget(rpiControlGroup);

  // 3. 상태
  QGroupBox *rpiStatusGroup = new QGroupBox(QString::fromUtf8("상태"), this);
  QFormLayout *rpiStatusForm = new QFormLayout();
  m_rpiConnectionStatusLabel =
      new QLabel(QString::fromUtf8("Disconnected"), this);
  m_rpiVehicleStatusLabel = new QLabel(QString::fromUtf8("-"), this);
  m_rpiLedStatusLabel = new QLabel(QString::fromUtf8("-"), this);
  m_rpiIrRawLabel = new QLabel(QString::fromUtf8("-"), this);
  m_rpiServoAngleLabel = new QLabel(QString::fromUtf8("-"), this);
  rpiStatusForm->addRow(QString::fromUtf8("연결 상태:"),
                        m_rpiConnectionStatusLabel);
  rpiStatusForm->addRow(QString::fromUtf8("차량 감지(IR):"),
                        m_rpiVehicleStatusLabel);
  rpiStatusForm->addRow(QString::fromUtf8("LED 상태:"), m_rpiLedStatusLabel);
  rpiStatusForm->addRow(QString::fromUtf8("IR Raw:"), m_rpiIrRawLabel);
  rpiStatusForm->addRow(QString::fromUtf8("서보 각도:"), m_rpiServoAngleLabel);
  rpiStatusGroup->setLayout(rpiStatusForm);
  rpiLayout->addWidget(rpiStatusGroup);

  rpiLayout->addStretch();

  // Tab 4: 주차 DB 현황판
  // ======================
  QWidget *parkingDbTab = new QWidget(this);
  QVBoxLayout *dbLayout = new QVBoxLayout(parkingDbTab);

  QTabWidget *dbSubTabs = new QTabWidget(this);

  // --- Sub-Tab 1: 주차 이력 (Parking Logs) ---
  QWidget *logsTab = new QWidget(this);
  QVBoxLayout *logsLayout = new QVBoxLayout(logsTab);

  QHBoxLayout *logsToolBar = new QHBoxLayout();
  m_plateSearchInput = new QLineEdit(this);
  m_plateSearchInput->setPlaceholderText("번호판 검색...");
  m_btnSearchPlate = new QPushButton("검색", this);
  m_btnRefreshLogs = new QPushButton("새로고침", this);
  logsToolBar->addWidget(m_plateSearchInput);
  logsToolBar->addWidget(m_btnSearchPlate);
  logsToolBar->addWidget(m_btnRefreshLogs);
  logsToolBar->addStretch();

  m_parkingLogTable = new QTableWidget(this);
  m_parkingLogTable->setColumnCount(5);
  m_parkingLogTable->setHorizontalHeaderLabels(
      QStringList() << "ID" << "번호판" << "ROI" << "입차시간" << "출차시간");
  m_parkingLogTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  m_parkingLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_parkingLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  logsLayout->addLayout(logsToolBar);
  logsLayout->addWidget(m_parkingLogTable);
  dbSubTabs->addTab(logsTab, "🚗 주차 이력");

  // --- Sub-Tab 2: 텔레그램 사용자 (Users) ---
  QWidget *usersTab = new QWidget(this);
  QVBoxLayout *usersLayout = new QVBoxLayout(usersTab);

  QHBoxLayout *usersToolBar = new QHBoxLayout();
  m_btnRefreshUsers = new QPushButton("새로고침", this);
  m_btnDeleteUser = new QPushButton("삭제", this);
  usersToolBar->addWidget(m_btnRefreshUsers);
  usersToolBar->addWidget(m_btnDeleteUser);
  usersToolBar->addStretch();

  m_userDbTable = new QTableWidget(this);
  m_userDbTable->setColumnCount(5);
  m_userDbTable->setHorizontalHeaderLabels(
      QStringList() << "Chat ID" << "번호판" << "이름" << "연락처" << "등록일");
  m_userDbTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_userDbTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_userDbTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  usersLayout->addLayout(usersToolBar);
  usersLayout->addWidget(m_userDbTable);
  dbSubTabs->addTab(usersTab, "👥 사용자");

  // --- Sub-Tab 3: 장치 로그 (Hardware Logs) ---
  QWidget *hwTab = new QWidget(this);
  QVBoxLayout *hwLayout = new QVBoxLayout(hwTab);

  QHBoxLayout *hwToolBar = new QHBoxLayout();
  m_btnRefreshHwLogs = new QPushButton("새로고침", this);
  m_btnClearHwLogs = new QPushButton("로그 비우기", this);
  hwToolBar->addWidget(m_btnRefreshHwLogs);
  hwToolBar->addWidget(m_btnClearHwLogs);
  hwToolBar->addStretch();

  m_hwLogTable = new QTableWidget(this);
  m_hwLogTable->setColumnCount(5);
  m_hwLogTable->setHorizontalHeaderLabels(
      QStringList() << "ID" << "구역" << "장치" << "동작" << "시간");
  m_hwLogTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_hwLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

  hwLayout->addLayout(hwToolBar);
  hwLayout->addWidget(m_hwLogTable);
  dbSubTabs->addTab(hwTab, "🔧 장치 로그");

  // --- Sub-Tab 4: 차량 정보 (Vehicles) ---
  QWidget *vhTab = new QWidget(this);
  QVBoxLayout *vhLayout = new QVBoxLayout(vhTab);

  QHBoxLayout *vhToolBar = new QHBoxLayout();
  m_btnRefreshVehicles = new QPushButton("새로고침", this);
  m_btnDeleteVehicle = new QPushButton("삭제", this);
  vhToolBar->addWidget(m_btnRefreshVehicles);
  vhToolBar->addWidget(m_btnDeleteVehicle);
  vhToolBar->addStretch();

  m_vehicleTable = new QTableWidget(this);
  m_vehicleTable->setColumnCount(5);
  m_vehicleTable->setHorizontalHeaderLabels(QStringList()
                                            << "번호판" << "차종" << "색상"
                                            << "지정여부" << "최근수정");
  m_vehicleTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  m_vehicleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_vehicleTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  vhLayout->addLayout(vhToolBar);
  vhLayout->addWidget(m_vehicleTable);
  dbSubTabs->addTab(vhTab, "🚘 차량 정보");

  // --- Sub-Tab 5: 주차구역 현황 (Zones) ---
  QWidget *zoneTab = new QWidget(this);
  QVBoxLayout *zoneLayout = new QVBoxLayout(zoneTab);

  QHBoxLayout *zoneToolBar = new QHBoxLayout();
  m_btnRefreshZone = new QPushButton("새로고침", this);
  zoneToolBar->addWidget(m_btnRefreshZone);
  zoneToolBar->addStretch();

  m_zoneTable = new QTableWidget(this);
  m_zoneTable->setColumnCount(4);
  m_zoneTable->setHorizontalHeaderLabels(QStringList()
                                         << "구역 ID" << "이름" << "용도"
                                         << "생성일");
  m_zoneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_zoneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_zoneTable->setSelectionBehavior(QAbstractItemView::SelectRows);

  zoneLayout->addLayout(zoneToolBar);
  zoneLayout->addWidget(m_zoneTable);
  dbSubTabs->addTab(zoneTab, "📍 주차구역 현황");

  dbLayout->addWidget(dbSubTabs);

  // 탭 추가
  // ======================
  // Tab 4: 객체 ReID (Debug)
  // ======================
  QWidget *reidTab = new QWidget(this);
  QVBoxLayout *reidLayout = new QVBoxLayout(reidTab);

  // 1. 실시간 객체 정보 테이블
  m_reidTable = new QTableWidget(this);
  m_reidTable->setColumnCount(5);
  m_reidTable->setHorizontalHeaderLabels(
      QStringList() << "ID" << "Type" << "Plate" << "Score" << "BBox");
  m_reidTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_reidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_reidTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  reidLayout->addWidget(m_reidTable);

  // 2. 선택 객체 정보 수정 (실험용 상세 제어)
  QGroupBox *forceGroup = new QGroupBox(
      QString::fromUtf8("선택 객체 정보 수정 (실험용 상세 제어)"), this);
  QVBoxLayout *forceLayout = new QVBoxLayout(); // 메인 레이아웃

  // 라벨 행
  QHBoxLayout *labelRow = new QHBoxLayout();
  labelRow->addWidget(new QLabel("ID", this), 1);
  labelRow->addWidget(new QLabel("Type", this), 2);
  labelRow->addWidget(new QLabel("Plate", this), 2);
  labelRow->addWidget(new QLabel("Score", this), 1);
  labelRow->addWidget(new QLabel("BBox (x y w h)", this), 3);
  labelRow->addWidget(new QLabel("", this), 1); // 버튼 공간

  // 입력 행
  QHBoxLayout *inputRow = new QHBoxLayout();

  m_forceObjectIdInput = new QSpinBox(this);
  m_forceObjectIdInput->setRange(0, 2147483647);
  inputRow->addWidget(m_forceObjectIdInput, 1);

  m_forceTypeInput = new QLineEdit(this);
  m_forceTypeInput->setPlaceholderText("Type");
  inputRow->addWidget(m_forceTypeInput, 2);

  m_forcePlateInput = new QLineEdit(this);
  m_forcePlateInput->setPlaceholderText("Plate");
  inputRow->addWidget(m_forcePlateInput, 2);

  m_forceScoreInput = new QDoubleSpinBox(this);
  m_forceScoreInput->setRange(0.0, 1.0);
  m_forceScoreInput->setSingleStep(0.01);
  inputRow->addWidget(m_forceScoreInput, 1);

  m_forceBBoxInput = new QLineEdit(this);
  m_forceBBoxInput->setPlaceholderText("x y w h");
  inputRow->addWidget(m_forceBBoxInput, 3);

  m_btnForcePlate = new QPushButton(QString::fromUtf8("정보 업데이트"), this);
  inputRow->addWidget(m_btnForcePlate, 1);

  forceLayout->addLayout(labelRow);
  forceLayout->addLayout(inputRow);
  forceGroup->setLayout(forceLayout);
  reidLayout->addWidget(forceGroup);

  // 표시 설정 그룹
  QGroupBox *settingsGroup =
      new QGroupBox(QString::fromUtf8("표시 및 보존 설정"), this);
  QHBoxLayout *settingsLayout = new QHBoxLayout();

  settingsLayout->addWidget(
      new QLabel(QString::fromUtf8("Stale Timeout (ms):"), this));
  m_staleTimeoutInput = new QSpinBox(this);
  m_staleTimeoutInput->setRange(0, 60000);
  m_staleTimeoutInput->setValue(1000);
  m_staleTimeoutInput->setSingleStep(500);
  settingsLayout->addWidget(m_staleTimeoutInput);

  settingsLayout->addSpacing(20);

  settingsLayout->addWidget(
      new QLabel(QString::fromUtf8("Prune Timeout (ms):"), this));
  m_pruneTimeoutInput = new QSpinBox(this);
  m_pruneTimeoutInput->setRange(0, 315360000); // 대략 1년(사실상 무제한)
  m_pruneTimeoutInput->setValue(5000);
  m_pruneTimeoutInput->setSingleStep(1000);
  settingsLayout->addWidget(m_pruneTimeoutInput);

  settingsLayout->addSpacing(20);
  m_chkShowStaleObjects =
      new QCheckBox(QString::fromUtf8("Stale 객체 표시"), this);
  m_chkShowStaleObjects->setChecked(true);
  settingsLayout->addWidget(m_chkShowStaleObjects);

  settingsLayout->addStretch();
  settingsGroup->setLayout(settingsLayout);
  reidLayout->addWidget(settingsGroup);

  tabWidget->addTab(cctvTab, QString::fromUtf8("📷 CCTV"));
  tabWidget->addTab(telegramTab, QString::fromUtf8("📱 텔레그램 테스트"));
  tabWidget->addTab(rpiTab, QString::fromUtf8("🧠 RPi 제어 테스트"));
  tabWidget->addTab(parkingDbTab, QString::fromUtf8("🗄️DB 조회"));
  tabWidget->addTab(reidTab, QString::fromUtf8("🔍 객체 ReID 테스트"));

  // 상위 레이아웃 구성
  layout->addWidget(tabWidget);

  // 공통 로그 (탭 아래)
  m_chkShowPlateLogs =
      new QCheckBox(QString::fromUtf8("번호판 인식 로그 표시"), this);
  m_chkShowPlateLogs->setChecked(true);
  layout->addWidget(m_chkShowPlateLogs);

  m_logView = new QTextEdit(this);
  m_logView->setReadOnly(true);
  m_logView->setMaximumHeight(120);
  layout->addWidget(m_logView);
}
