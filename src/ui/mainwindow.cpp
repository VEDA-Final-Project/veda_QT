#include "mainwindow.h"
#include <QDate>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QVBoxLayout *layout = new QVBoxLayout(centralWidget);

  QHBoxLayout *btnLayout = new QHBoxLayout();
  QPushButton *btnPlay = new QPushButton("CCTV 보기", this);
  QPushButton *btnExit = new QPushButton("종료", this);
  QPushButton *btnApplyRoi = new QPushButton("Draw ROI (Polygon)", this);
  QPushButton *btnFinishRoi = new QPushButton("ROI 완료", this);

  btnLayout->addWidget(btnPlay);
  btnLayout->addWidget(btnExit);
  btnLayout->addSpacing(20);
  btnLayout->addWidget(btnApplyRoi);
  btnLayout->addWidget(btnFinishRoi);
  layout->addLayout(btnLayout);

  m_cameraManager = new CameraManager(this);
  m_videoWidget = new VideoWidget(this);
  layout->addWidget(m_videoWidget);

  m_logView = new QTextEdit(this);
  m_logView->setReadOnly(true);
  m_logView->setMaximumHeight(120);
  layout->addWidget(m_logView);

  connect(btnPlay, &QPushButton::clicked, this, &MainWindow::playCctv);
  connect(btnExit, &QPushButton::clicked, this, &MainWindow::close);
  connect(btnApplyRoi, &QPushButton::clicked, this, [this]() {
    m_videoWidget->startRoiDrawing();
    m_logView->append("[ROI] Draw mode: left-click points, then press 'ROI 완료'.");
  });
  connect(btnFinishRoi, &QPushButton::clicked, this, [this]() {
    if (!m_videoWidget->completeRoiDrawing())
    {
      m_logView->append("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
    }
  });

  // Plain text bbox log for convenience.
  connect(m_videoWidget, &VideoWidget::roiChanged, this,
          [this](const QRect &roi) {
            m_logView->append(
                QString("[ROI] bbox x:%1 y:%2 w:%3 h:%4")
                    .arg(roi.x())
                    .arg(roi.y())
                    .arg(roi.width())
                    .arg(roi.height()));
          });

  // Structured log for finalized ROI only.
  connect(m_videoWidget, &VideoWidget::roiPolygonChanged, this,
          [this](const QPolygon &polygon) {
            QJsonArray points;
            for (const QPoint &pt : polygon)
            {
              points.append(QJsonObject{{"x", pt.x()}, {"y", pt.y()}});
            }

            const QRect bbox = polygon.boundingRect();
            ++m_roiSequence;
            QJsonObject roiData{
                {"roi_id", m_roiSequence},
                {"location", QJsonObject{
                                 {"point_count", polygon.size()},
                                 {"points", points},
                                 {"bbox", QJsonObject{
                                              {"x", bbox.x()},
                                              {"y", bbox.y()},
                                              {"w", bbox.width()},
                                              {"h", bbox.height()},
                                          }},
                             }},
                {"purpose", QString::fromUtf8("미정")},
                {"date", QDate::currentDate().toString(Qt::ISODate)},
            };
            appendRoiStructuredLog(roiData);
          });

  connect(m_cameraManager, &CameraManager::frameCaptured, m_videoWidget,
          &VideoWidget::updateFrame);
  connect(m_cameraManager, &CameraManager::metadataReceived, m_videoWidget,
          &VideoWidget::updateMetadata);
  connect(m_cameraManager, &CameraManager::logMessage, this,
          &MainWindow::onLogMessage);
  connect(m_videoWidget, &VideoWidget::ocrResult, this,
          &MainWindow::onOcrResult);

  resize(1000, 700);
}

MainWindow::~MainWindow()
{
}

void MainWindow::playCctv()
{
  if (m_cameraManager->isRunning())
  {
    m_cameraManager->restart();
    return;
  }
  m_cameraManager->start();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  m_cameraManager->stop();
  QMainWindow::closeEvent(event);
}

void MainWindow::onLogMessage(const QString &msg)
{
  qDebug() << "[Camera]" << msg;
  m_logView->append(msg);
}

void MainWindow::onOcrResult(int objectId, const QString &result)
{
  const QString msg = QString("[OCR] ID:%1 Result:%2").arg(objectId).arg(result);
  qDebug() << msg;
  m_logView->append(msg);
}

void MainWindow::appendRoiStructuredLog(const QJsonObject &roiData)
{
  const QString line =
      QString::fromUtf8(QJsonDocument(roiData).toJson(QJsonDocument::Compact));
  qDebug().noquote() << line;
  m_logView->append(line);
}
