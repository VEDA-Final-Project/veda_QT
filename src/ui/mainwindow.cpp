#include "mainwindow.h"
#include "config.h"
#include <QDate>
#include <QDebug>
#include <QDateTime>
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
  m_ocrCoordinator = new PlateOcrCoordinator(this);
  m_videoWidget = new VideoWidget(this);
  layout->addWidget(m_videoWidget);
  m_metadataSynchronizer.setDelayMs(Config::instance().defaultDelayMs());

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

  connect(m_cameraManager, &CameraManager::metadataReceived, this,
          [this](const QList<ObjectInfo> &objects) {
            m_metadataSynchronizer.pushMetadata(
                objects, QDateTime::currentMSecsSinceEpoch());
          });
  connect(m_cameraManager, &CameraManager::frameCaptured, this,
          [this](const QImage &frame) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            m_videoWidget->updateMetadata(m_metadataSynchronizer.consumeReady(nowMs));
            m_videoWidget->updateFrame(frame);
          });
  connect(m_cameraManager, &CameraManager::logMessage, this,
          &MainWindow::onLogMessage);
  connect(m_videoWidget, &VideoWidget::ocrRequested, m_ocrCoordinator,
          &PlateOcrCoordinator::requestOcr);
  connect(m_ocrCoordinator, &PlateOcrCoordinator::ocrReady, this,
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
  flushSuppressedCameraLogs();
  m_cameraManager->stop();
  QMainWindow::closeEvent(event);
}

void MainWindow::onLogMessage(const QString &msg)
{
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  constexpr qint64 kDuplicateWindowMs = 2000;

  const bool isRapidDuplicate =
      (msg == m_lastCameraLogMessage) && (m_lastCameraLogMs > 0) &&
      ((nowMs - m_lastCameraLogMs) < kDuplicateWindowMs);

  if (isRapidDuplicate)
  {
    ++m_suppressedCameraLogCount;
    m_lastCameraLogMs = nowMs;
    return;
  }

  flushSuppressedCameraLogs();
  m_lastCameraLogMessage = msg;
  m_lastCameraLogMs = nowMs;

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

void MainWindow::flushSuppressedCameraLogs()
{
  if (m_suppressedCameraLogCount <= 0 || m_lastCameraLogMessage.isEmpty())
  {
    return;
  }

  const QString summary =
      QString("[Camera] previous log repeated %1 times: %2")
          .arg(m_suppressedCameraLogCount)
          .arg(m_lastCameraLogMessage);
  qDebug() << summary;
  m_logView->append(summary);
  m_suppressedCameraLogCount = 0;
}
