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
  setupUi();

  if (m_btnExit)
  {
    connect(m_btnExit, &QPushButton::clicked, this, &MainWindow::close);
  }

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
  m_controller = new MainWindowController(uiRefs, this);

  resize(1000, 700);
}

MainWindow::~MainWindow()
{
}

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
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);
  QVBoxLayout *layout = new QVBoxLayout(centralWidget);

  QHBoxLayout *btnLayout = new QHBoxLayout();
  m_btnPlay = new QPushButton("CCTV 보기", this);
  m_btnExit = new QPushButton("종료", this);
  m_btnApplyRoi = new QPushButton("Draw ROI (Polygon)", this);
  m_btnFinishRoi = new QPushButton("ROI 완료", this);

  QLabel *nameLabel = new QLabel("이름:", this);
  m_roiNameEdit = new QLineEdit(this);
  m_roiNameEdit->setPlaceholderText(QStringLiteral("ROI 이름 입력(필수)"));
  m_roiNameEdit->setClearButtonEnabled(true);
  m_roiNameEdit->setMaxLength(30);
  m_roiNameEdit->setMinimumWidth(180);
  m_roiNameEdit->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("^[A-Za-z0-9가-힣 _-]{0,30}$")),
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
  layout->addLayout(btnLayout);

  m_videoWidget = new VideoWidget(this);
  layout->addWidget(m_videoWidget);

  m_logView = new QTextEdit(this);
  m_logView->setReadOnly(true);
  m_logView->setMaximumHeight(120);
  layout->addWidget(m_logView);
}
