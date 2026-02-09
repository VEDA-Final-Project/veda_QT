#include "mainwindow.h"
#include <QDebug>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QVBoxLayout *layout = new QVBoxLayout(centralWidget);

  // 버튼 레이아웃 (가로 배치)
  QHBoxLayout *btnLayout = new QHBoxLayout();
  QPushButton *btnPlay = new QPushButton("CCTV 보기", this);
  QPushButton *btnExit = new QPushButton("종료", this);

  // 싱크 조절 UI
  QPushButton *btnSyncDown = new QPushButton("<< Sync(-)", this);
  QPushButton *btnSyncUp = new QPushButton("Sync(+) >>", this);
  m_lblSync = new QLabel("Delay: 0ms", this);

  btnLayout->addWidget(btnPlay);
  btnLayout->addWidget(btnExit);
  btnLayout->addSpacing(20);
  btnLayout->addWidget(btnSyncDown);
  btnLayout->addWidget(m_lblSync);
  btnLayout->addWidget(btnSyncUp);

  layout->addLayout(btnLayout);

  // Helper 클래스 초기화
  m_cameraManager = new CameraManager(this);
  m_videoWidget = new VideoWidget(this); // QLabel 상속받음

  // VideoWidget을 레이아웃에 추가
  layout->addWidget(m_videoWidget);

  // 로그 출력용 TextEdit
  m_logView = new QTextEdit(this);
  m_logView->setReadOnly(true);
  m_logView->setMaximumHeight(100);
  layout->addWidget(m_logView);

  m_syncDelayMs = 0; // 초기 딜레이 0ms

  // 버튼 연결
  connect(btnPlay, &QPushButton::clicked, this, &MainWindow::playCctv);
  connect(btnExit, &QPushButton::clicked, this, &MainWindow::close);
  connect(btnSyncDown, &QPushButton::clicked, this, &MainWindow::decreaseSync);
  connect(btnSyncUp, &QPushButton::clicked, this, &MainWindow::increaseSync);

  // CameraManager -> VideoWidget 직접 연결 (프레임, 메타데이터 전달)
  connect(m_cameraManager, &CameraManager::frameCaptured, m_videoWidget,
          &VideoWidget::updateFrame);
  connect(m_cameraManager, &CameraManager::metadataReceived, m_videoWidget,
          &VideoWidget::updateMetadata);

  // 로그 연결
  connect(m_cameraManager, &CameraManager::logMessage, this,
          &MainWindow::onLogMessage);

  // OCR 결과 연결
  connect(m_videoWidget, &VideoWidget::ocrResult, this,
          &MainWindow::onOcrResult);

  resize(800, 600);
}

MainWindow::~MainWindow() {
  // VideoWidget, CameraManager 등은 QObject 트리 구조에 의해 자동 삭제됨
}

void MainWindow::playCctv() {
  if (m_cameraManager->isRunning()) {
    m_cameraManager->restart();
    return;
  }
  m_cameraManager->start();
}

void MainWindow::closeEvent(QCloseEvent *event) {
  // Ensure worker threads shut down before the window closes.
  m_cameraManager->stop();
  QMainWindow::closeEvent(event);
}

void MainWindow::decreaseSync() {
  m_syncDelayMs -= 100;
  if (m_syncDelayMs < 0)
    m_syncDelayMs = 0;
  updateSyncLabel();
}

void MainWindow::increaseSync() {
  m_syncDelayMs += 100;
  updateSyncLabel();
}

void MainWindow::updateSyncLabel() {
  m_lblSync->setText(QString("Delay: %1ms").arg(m_syncDelayMs));
  m_videoWidget->setSyncDelay(m_syncDelayMs);
}

void MainWindow::onLogMessage(const QString &msg) {
  qDebug() << "[Camera]" << msg;
  // UI 로그뷰에 추가하고 싶으면 여기에 작성
  m_logView->append(msg);
}

void MainWindow::onOcrResult(int objectId, const QString &result) {
  QString msg = QString("[OCR] ID:%1 Result:%2").arg(objectId).arg(result);
  m_logView->append(msg);
}
