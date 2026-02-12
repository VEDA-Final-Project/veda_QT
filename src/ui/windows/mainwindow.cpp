#include "mainwindow.h"
#include "mainwindowcontroller.h"

#include <QComboBox>
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

  // Controller 생성 (MainWindow가 부모 → 자동 메모리 관리)
  m_controller = new MainWindowController(uiRefs, this);

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
  m_rpiHostEdit->setPlaceholderText(QStringLiteral("RPi Host (예: 192.168.0.50)"));
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
  m_rpiConnectionStatusLabel = new QLabel(QString::fromUtf8("Disconnected"), this);
  m_rpiVehicleStatusLabel = new QLabel(QString::fromUtf8("-"), this);
  m_rpiLedStatusLabel = new QLabel(QString::fromUtf8("-"), this);
  m_rpiIrRawLabel = new QLabel(QString::fromUtf8("-"), this);
  m_rpiServoAngleLabel = new QLabel(QString::fromUtf8("-"), this);
  rpiStatusForm->addRow(QString::fromUtf8("연결 상태:"), m_rpiConnectionStatusLabel);
  rpiStatusForm->addRow(QString::fromUtf8("차량 감지(IR):"), m_rpiVehicleStatusLabel);
  rpiStatusForm->addRow(QString::fromUtf8("LED 상태:"), m_rpiLedStatusLabel);
  rpiStatusForm->addRow(QString::fromUtf8("IR Raw:"), m_rpiIrRawLabel);
  rpiStatusForm->addRow(QString::fromUtf8("서보 각도:"), m_rpiServoAngleLabel);
  rpiStatusGroup->setLayout(rpiStatusForm);
  rpiLayout->addWidget(rpiStatusGroup);

  rpiLayout->addStretch();

  // 탭 추가
  tabWidget->addTab(cctvTab, QString::fromUtf8("📷 CCTV"));
  tabWidget->addTab(telegramTab, QString::fromUtf8("📱 텔레그램 테스트"));
  tabWidget->addTab(rpiTab, QString::fromUtf8("🧠 RPi 제어"));

  // 상위 레이아웃 구성
  layout->addWidget(tabWidget);

  // 공통 로그 (탭 아래)
  m_logView = new QTextEdit(this);
  m_logView->setReadOnly(true);
  m_logView->setMaximumHeight(120);
  layout->addWidget(m_logView);
}
