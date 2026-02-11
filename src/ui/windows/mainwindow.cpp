#include "mainwindow.h"
#include "mainwindowcontroller.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
  // UI 레이아웃 및 위젯 생성
  setupUi();

  // 종료 버튼이 존재하면 클릭 시 창 닫기
  if (m_btnExit)
  {
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
void MainWindow::closeEvent(QCloseEvent *event)
{
  if (m_controller)
  {
    m_controller->shutdown();
  }
  QMainWindow::closeEvent(event);
}

void MainWindow::setupUi()
{
  // 메인 윈도우의 중앙 위젯 설정
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  // 전체를 감싸는 세로 레이아웃
  QVBoxLayout *layout = new QVBoxLayout(centralWidget);

  // ======================
  // 상단 버튼 / 입력 영역
  // ======================
  QHBoxLayout *btnLayout = new QHBoxLayout();

  // 기본 제어 버튼
  m_btnPlay = new QPushButton("CCTV 보기", this);
  m_btnExit = new QPushButton("종료", this);
  m_btnApplyRoi = new QPushButton("주차 구역 설정하기", this);
  m_btnFinishRoi = new QPushButton("ROI 완료", this);

  // ROI 이름 입력
  QLabel *nameLabel = new QLabel("이름:", this);
  m_roiNameEdit = new QLineEdit(this);
  m_roiNameEdit->setPlaceholderText(QStringLiteral("ROI 이름 입력(필수)"));
  m_roiNameEdit->setClearButtonEnabled(true);
  m_roiNameEdit->setMaxLength(20);
  m_roiNameEdit->setMinimumWidth(180);

  /*
   * ROI 이름 유효성 검사
   * - 한글, 영문, 숫자, 공백, _, - 허용
   * - 최대 20자
   */
  m_roiNameEdit->setValidator(
      new QRegularExpressionValidator(
          QRegularExpression(QStringLiteral("^[A-Za-z0-9가-힣 _-]{0,20}$")),
          m_roiNameEdit));

  // ROI 목적 선택
  QLabel *purposeLabel = new QLabel("목적:", this);
  m_roiPurposeCombo = new QComboBox(this);
  m_roiPurposeCombo->addItem(QStringLiteral("지정 주차"));
  m_roiPurposeCombo->addItem(QStringLiteral("일반 주차"));
  m_roiPurposeCombo->setMinimumWidth(120);

  // ROI 선택 콤보박스
  QLabel *roiLabel = new QLabel("ROI:", this);
  m_roiSelectorCombo = new QComboBox(this);
  m_roiSelectorCombo->setMinimumContentsLength(24);
  m_roiSelectorCombo->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_roiSelectorCombo->setMinimumWidth(260);
  m_roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

  // ROI 삭제 버튼
  m_btnDeleteRoi = new QPushButton("ROI 삭제", this);

  // 레이아웃에 위젯 배치
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

  // 상단 영역을 메인 레이아웃에 추가
  layout->addLayout(btnLayout);

  // ======================
  // 비디오 표시 영역
  // ======================
  m_videoWidget = new VideoWidget(this);
  layout->addWidget(m_videoWidget);

  // ======================
  // 로그 출력 영역
  // ======================
  m_logView = new QTextEdit(this);
  m_logView->setReadOnly(true);
  m_logView->setMaximumHeight(120);
  layout->addWidget(m_logView);
}
