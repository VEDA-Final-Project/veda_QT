#include "mainwindowcontroller.h"

#include "core/config.h"
#include "ui/video/videowidget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QLineEdit>

MainWindowController::MainWindowController(const UiRefs &uiRefs, QObject *parent)
    : QObject(parent), m_ui(uiRefs)
{
  m_cameraManager = new CameraManager(this);
  m_ocrCoordinator = new PlateOcrCoordinator(this);
  m_cameraSession.setCameraManager(m_cameraManager);
  m_cameraSession.setDelayMs(Config::instance().defaultDelayMs());
  initRoiDb();
  connectSignals();
}

void MainWindowController::shutdown()
{
  const QString summary = m_logDeduplicator.flushPending();
  if (!summary.isEmpty())
  {
    qDebug() << summary;
    if (m_ui.logView)
    {
      m_ui.logView->append(summary);
    }
  }
  m_cameraSession.stop();
}

void MainWindowController::connectSignals()
{
  if (m_ui.btnPlay)
  {
    connect(m_ui.btnPlay, &QPushButton::clicked, this,
            &MainWindowController::playCctv);
  }
  if (m_ui.btnApplyRoi)
  {
    connect(m_ui.btnApplyRoi, &QPushButton::clicked, this,
            &MainWindowController::onStartRoiDraw);
  }
  if (m_ui.btnFinishRoi)
  {
    connect(m_ui.btnFinishRoi, &QPushButton::clicked, this,
            &MainWindowController::onFinishRoiDraw);
  }
  if (m_ui.btnDeleteRoi)
  {
    connect(m_ui.btnDeleteRoi, &QPushButton::clicked, this,
            &MainWindowController::onDeleteRoi);
  }
  if (m_ui.videoWidget)
  {
    connect(m_ui.videoWidget, &VideoWidget::roiChanged, this,
            &MainWindowController::onRoiChanged);
    connect(m_ui.videoWidget, &VideoWidget::roiPolygonChanged, this,
            &MainWindowController::onRoiPolygonChanged);
    connect(m_ui.videoWidget, &VideoWidget::ocrRequested, m_ocrCoordinator,
            &PlateOcrCoordinator::requestOcr);
  }
  connect(m_cameraManager, &CameraManager::metadataReceived, this,
          &MainWindowController::onMetadataReceived);
  connect(m_cameraManager, &CameraManager::frameCaptured, this,
          &MainWindowController::onFrameCaptured);
  connect(m_cameraManager, &CameraManager::logMessage, this,
          &MainWindowController::onLogMessage);
  connect(m_ocrCoordinator, &PlateOcrCoordinator::ocrReady, this,
          &MainWindowController::onOcrResult);
}

void MainWindowController::initRoiDb()
{
  if (!m_ui.logView)
  {
    return;
  }

  const QString roiDbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/roi.sqlite");
  const RoiService::InitResult initResult = m_roiService.init(roiDbPath);
  if (!initResult.ok)
  {
    m_ui.logView->append(QString("[ROI][DB] 초기화 실패: %1").arg(initResult.error));
    return;
  }

  if (m_ui.videoWidget)
  {
    m_ui.videoWidget->queueNormalizedRoiPolygons(initResult.normalizedPolygons);
  }

  refreshRoiSelector();
  if (initResult.loadedCount > 0)
  {
    m_ui.logView->append(QString("[ROI][DB] %1개 ROI 로드 완료").arg(initResult.loadedCount));
  }
}

void MainWindowController::appendRoiStructuredLog(const QJsonObject &roiData)
{
  if (!m_ui.logView)
  {
    return;
  }
  const QString line =
      QString::fromUtf8(QJsonDocument(roiData).toJson(QJsonDocument::Compact));
  qDebug().noquote() << line;
  m_ui.logView->append(line);
}

void MainWindowController::refreshRoiSelector()
{
  if (!m_ui.roiSelectorCombo)
  {
    return;
  }
  m_ui.roiSelectorCombo->clear();
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

  const QVector<QJsonObject> &records = m_roiService.records();
  for (int i = 0; i < records.size(); ++i)
  {
    const QJsonObject &record = records[i];
    const QString name = record["rod_name"].toString(QString("rod_%1").arg(i + 1));
    const QString purpose = record["rod_purpose"].toString();
    m_ui.roiSelectorCombo->addItem(QString("%1 | %2").arg(name, purpose), i);
  }
}

void MainWindowController::playCctv()
{
  m_cameraSession.playOrRestart();
}

void MainWindowController::onLogMessage(const QString &msg)
{
  if (!m_ui.logView)
  {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const LogDeduplicator::IngestResult ingestResult =
      m_logDeduplicator.ingest(msg, nowMs);

  if (!ingestResult.flushSummary.isEmpty())
  {
    qDebug() << ingestResult.flushSummary;
    m_ui.logView->append(ingestResult.flushSummary);
  }
  if (ingestResult.suppressed)
  {
    return;
  }

  qDebug() << "[Camera]" << msg;
  m_ui.logView->append(msg);
}

void MainWindowController::onOcrResult(int objectId, const QString &result)
{
  if (!m_ui.logView)
  {
    return;
  }
  const QString msg = QString("[OCR] ID:%1 Result:%2").arg(objectId).arg(result);
  qDebug() << msg;
  m_ui.logView->append(msg);
}

void MainWindowController::onStartRoiDraw()
{
  if (!m_ui.videoWidget || !m_ui.logView)
  {
    return;
  }
  m_ui.videoWidget->startRoiDrawing();
  m_ui.logView->append("[ROI] Draw mode: left-click points, then press 'ROI 완료'.");
}

void MainWindowController::onFinishRoiDraw()
{
  if (!m_ui.videoWidget || !m_ui.logView)
  {
    return;
  }
  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  QString nameError;
  if (!m_roiService.isValidName(typedName, &nameError))
  {
    m_ui.logView->append(QString("[ROI] 완료 실패: %1").arg(nameError));
    return;
  }
  if (m_roiService.isDuplicateName(typedName))
  {
    m_ui.logView->append(
        QString("[ROI] 완료 실패: 이름 '%1' 이(가) 이미 존재합니다.").arg(typedName));
    return;
  }
  if (!m_ui.videoWidget->completeRoiDrawing())
  {
    m_ui.logView->append("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
  }
}

void MainWindowController::onDeleteRoi()
{
  if (!m_ui.roiSelectorCombo || !m_ui.videoWidget || !m_ui.logView)
  {
    return;
  }
  const int recordIndex = m_ui.roiSelectorCombo->currentData().toInt();
  if (recordIndex < 0 || recordIndex >= m_roiService.count())
  {
    m_ui.logView->append("[ROI] 삭제 실패: ROI를 선택해주세요.");
    return;
  }

  const RoiService::DeleteResult deleteResult = m_roiService.removeAt(recordIndex);
  if (!deleteResult.ok)
  {
    m_ui.logView->append(QString("[ROI][DB] 삭제 실패: %1").arg(deleteResult.error));
    return;
  }
  if (!m_ui.videoWidget->removeRoiAt(recordIndex))
  {
    m_ui.logView->append("[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
    return;
  }

  refreshRoiSelector();
  int nextRecordIndex = recordIndex;
  if (nextRecordIndex >= m_roiService.count())
  {
    nextRecordIndex = m_roiService.count() - 1;
  }
  const int comboIndex =
      (nextRecordIndex >= 0) ? m_ui.roiSelectorCombo->findData(nextRecordIndex) : -1;
  m_ui.roiSelectorCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
  m_ui.logView->append(QString("[ROI] 삭제 완료: %1").arg(deleteResult.removedName));
}

void MainWindowController::onRoiChanged(const QRect &roi)
{
  if (!m_ui.logView)
  {
    return;
  }
  m_ui.logView->append(
      QString("[ROI] bbox x:%1 y:%2 w:%3 h:%4")
          .arg(roi.x())
          .arg(roi.y())
          .arg(roi.width())
          .arg(roi.height()));
}

void MainWindowController::onRoiPolygonChanged(const QPolygon &polygon,
                                               const QSize &frameSize)
{
  if (!m_ui.logView)
  {
    return;
  }
  if (frameSize.isEmpty())
  {
    m_ui.logView->append("[ROI] 저장 실패: 프레임 크기가 유효하지 않습니다.");
    return;
  }

  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  const QString purpose =
      m_ui.roiPurposeCombo ? m_ui.roiPurposeCombo->currentText() : QString();

  const RoiService::CreateResult createResult =
      m_roiService.createFromPolygon(polygon, frameSize, typedName, purpose);
  if (!createResult.ok)
  {
    if (m_ui.videoWidget && m_ui.videoWidget->roiCount() > 0)
    {
      m_ui.videoWidget->removeRoiAt(m_ui.videoWidget->roiCount() - 1);
    }
    m_ui.logView->append(QString("[ROI][DB] 저장 실패: %1").arg(createResult.error));
    refreshRoiSelector();
    return;
  }

  refreshRoiSelector();
  if (m_ui.roiSelectorCombo)
  {
    m_ui.roiSelectorCombo->setCurrentIndex(m_ui.roiSelectorCombo->count() - 1);
  }
  appendRoiStructuredLog(createResult.record);
}

void MainWindowController::onMetadataReceived(const QList<ObjectInfo> &objects)
{
  m_cameraSession.pushMetadata(objects, QDateTime::currentMSecsSinceEpoch());
}

void MainWindowController::onFrameCaptured(const QImage &frame)
{
  if (!m_ui.videoWidget)
  {
    return;
  }
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_ui.videoWidget->updateMetadata(m_cameraSession.consumeReadyMetadata(nowMs));
  m_ui.videoWidget->updateFrame(frame);
}
