#include "mainwindowcontroller.h"

#include "core/config.h"
#include "ui/video/videowidget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QRegularExpression>
#include <QtGlobal>

MainWindowController::MainWindowController(const UiRefs &uiRefs, QObject *parent)
    : QObject(parent), m_ui(uiRefs)
{
  m_cameraManager = new CameraManager(this);
  m_ocrCoordinator = new PlateOcrCoordinator(this);
  m_metadataSynchronizer.setDelayMs(Config::instance().defaultDelayMs());
  initRoiDb();
  connectSignals();
}

void MainWindowController::shutdown()
{
  flushSuppressedCameraLogs();
  if (m_cameraManager)
  {
    m_cameraManager->stop();
  }
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
  QString dbError;
  if (!m_roiRepository.init(roiDbPath, &dbError))
  {
    m_ui.logView->append(QString("[ROI][DB] 초기화 실패: %1").arg(dbError));
    return;
  }

  m_roiRecords = m_roiRepository.loadAll(&dbError);
  if (!dbError.isEmpty())
  {
    m_ui.logView->append(QString("[ROI][DB] 로드 실패: %1").arg(dbError));
    return;
  }

  QList<QPolygonF> normalizedPolygons;
  normalizedPolygons.reserve(m_roiRecords.size());
  for (const QJsonObject &record : m_roiRecords)
  {
    const QJsonArray points = record["rod_points"].toArray();
    if (points.size() < 3)
    {
      continue;
    }
    QPolygonF polygon;
    for (const QJsonValue &pointValue : points)
    {
      const QJsonObject pointObj = pointValue.toObject();
      polygon << QPointF(pointObj["x"].toDouble(), pointObj["y"].toDouble());
    }
    if (polygon.size() >= 3)
    {
      normalizedPolygons.append(polygon);
    }
  }
  if (m_ui.videoWidget)
  {
    m_ui.videoWidget->queueNormalizedRoiPolygons(normalizedPolygons);
  }

  for (const QJsonObject &record : m_roiRecords)
  {
    const QString rodId = record["rod_id"].toString();
    if (!rodId.startsWith("rod-"))
    {
      continue;
    }
    bool ok = false;
    const int seq = rodId.mid(4).toInt(&ok);
    if (ok)
    {
      m_roiSequence = qMax(m_roiSequence, seq);
    }
  }

  refreshRoiSelector();
  if (!m_roiRecords.isEmpty())
  {
    m_ui.logView->append(QString("[ROI][DB] %1개 ROI 로드 완료").arg(m_roiRecords.size()));
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

void MainWindowController::flushSuppressedCameraLogs()
{
  if (!m_ui.logView)
  {
    return;
  }
  if (m_suppressedCameraLogCount <= 0 || m_lastCameraLogMessage.isEmpty())
  {
    return;
  }

  const QString summary =
      QString("[Camera] previous log repeated %1 times: %2")
          .arg(m_suppressedCameraLogCount)
          .arg(m_lastCameraLogMessage);
  qDebug() << summary;
  m_ui.logView->append(summary);
  m_suppressedCameraLogCount = 0;
}

void MainWindowController::refreshRoiSelector()
{
  if (!m_ui.roiSelectorCombo)
  {
    return;
  }
  m_ui.roiSelectorCombo->clear();
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);
  for (int i = 0; i < m_roiRecords.size(); ++i)
  {
    const QJsonObject &record = m_roiRecords[i];
    const QString name = record["rod_name"].toString(QString("rod_%1").arg(i + 1));
    const QString purpose = record["rod_purpose"].toString();
    m_ui.roiSelectorCombo->addItem(QString("%1 | %2").arg(name, purpose), i);
  }
}

bool MainWindowController::isValidRoiName(const QString &name,
                                          QString *errorMessage) const
{
  if (name.isEmpty())
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI 이름은 필수입니다.");
    }
    return false;
  }

  constexpr int kMinNameLen = 1;
  constexpr int kMaxNameLen = 30;
  if (name.size() < kMinNameLen || name.size() > kMaxNameLen)
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI 이름은 1~30자로 입력해주세요.");
    }
    return false;
  }

  static const QRegularExpression kAllowedNamePattern(
      QStringLiteral("^[A-Za-z0-9가-힣 _-]+$"));
  if (!kAllowedNamePattern.match(name).hasMatch())
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral(
          "ROI 이름은 한글/영문/숫자/공백/밑줄(_) / 하이픈(-)만 사용할 수 있습니다.");
    }
    return false;
  }

  return true;
}

bool MainWindowController::isDuplicateRoiName(const QString &name) const
{
  for (const QJsonObject &record : m_roiRecords)
  {
    if (record["rod_name"].toString().compare(name, Qt::CaseInsensitive) == 0)
    {
      return true;
    }
  }
  return false;
}

void MainWindowController::playCctv()
{
  if (!m_cameraManager)
  {
    return;
  }
  if (m_cameraManager->isRunning())
  {
    m_cameraManager->restart();
    return;
  }
  m_cameraManager->start();
}

void MainWindowController::onLogMessage(const QString &msg)
{
  if (!m_ui.logView)
  {
    return;
  }
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
  if (!isValidRoiName(typedName, &nameError))
  {
    m_ui.logView->append(QString("[ROI] 완료 실패: %1").arg(nameError));
    return;
  }
  if (isDuplicateRoiName(typedName))
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
  if (recordIndex < 0 || recordIndex >= m_roiRecords.size())
  {
    m_ui.logView->append("[ROI] 삭제 실패: ROI를 선택해주세요.");
    return;
  }
  const QString removedId = m_roiRecords[recordIndex]["rod_id"].toString();
  QString dbError;
  if (!m_roiRepository.removeById(removedId, &dbError))
  {
    m_ui.logView->append(QString("[ROI][DB] 삭제 실패: %1").arg(dbError));
    return;
  }
  if (!m_ui.videoWidget->removeRoiAt(recordIndex))
  {
    m_ui.logView->append("[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
    return;
  }
  const QString removedName = m_roiRecords[recordIndex]["rod_name"].toString();
  m_roiRecords.removeAt(recordIndex);
  refreshRoiSelector();
  int nextRecordIndex = recordIndex;
  if (nextRecordIndex >= m_roiRecords.size())
  {
    nextRecordIndex = m_roiRecords.size() - 1;
  }
  const int comboIndex =
      (nextRecordIndex >= 0) ? m_ui.roiSelectorCombo->findData(nextRecordIndex) : -1;
  m_ui.roiSelectorCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
  m_ui.logView->append(QString("[ROI] 삭제 완료: %1").arg(removedName));
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
  if (!m_ui.logView || frameSize.isEmpty())
  {
    if (m_ui.logView && frameSize.isEmpty())
    {
      m_ui.logView->append("[ROI] 저장 실패: 프레임 크기가 유효하지 않습니다.");
    }
    return;
  }

  const double frameW = static_cast<double>(frameSize.width());
  const double frameH = static_cast<double>(frameSize.height());
  auto normX = [frameW](int x) {
    return qBound(0.0, static_cast<double>(x) / frameW, 1.0);
  };
  auto normY = [frameH](int y) {
    return qBound(0.0, static_cast<double>(y) / frameH, 1.0);
  };

  QJsonArray points;
  for (const QPoint &pt : polygon)
  {
    points.append(QJsonObject{{"x", normX(pt.x())}, {"y", normY(pt.y())}});
  }

  const QRect bbox = polygon.boundingRect();
  const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
  const QString ts = nowUtc.toString(Qt::ISODate);
  ++m_roiSequence;
  const QString rodId =
      QString("rod-%1").arg(m_roiSequence, 3, 10, QLatin1Char('0'));
  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  QString nameError;
  if (!isValidRoiName(typedName, &nameError))
  {
    m_ui.logView->append(QString("[ROI] 생성 실패: %1").arg(nameError));
    return;
  }
  if (isDuplicateRoiName(typedName))
  {
    m_ui.logView->append(
        QString("[ROI] 생성 실패: 이름 '%1' 이(가) 이미 존재합니다.").arg(typedName));
    return;
  }
  QJsonObject roiData{
      {"rod_id", rodId},
      {"rod_name", typedName},
      {"rod_enable", true},
      {"rod_purpose", m_ui.roiPurposeCombo
                          ? m_ui.roiPurposeCombo->currentText()
                          : QStringLiteral("일반 주차")},
      {"rod_points", points},
      {"bbox",
       QJsonObject{
           {"x", normX(bbox.x())},
           {"y", normY(bbox.y())},
           {"w", qBound(0.0, static_cast<double>(bbox.width()) / frameW, 1.0)},
           {"h", qBound(0.0, static_cast<double>(bbox.height()) / frameH, 1.0)},
       }},
      {"created_at", ts},
  };
  m_roiRecords.append(roiData);
  QString dbError;
  if (!m_roiRepository.upsert(roiData, &dbError))
  {
    m_roiRecords.removeLast();
    if (m_ui.videoWidget->roiCount() > 0)
    {
      m_ui.videoWidget->removeRoiAt(m_ui.videoWidget->roiCount() - 1);
    }
    m_ui.logView->append(QString("[ROI][DB] 저장 실패: %1").arg(dbError));
    refreshRoiSelector();
    return;
  }
  refreshRoiSelector();
  if (m_ui.roiSelectorCombo)
  {
    m_ui.roiSelectorCombo->setCurrentIndex(m_ui.roiSelectorCombo->count() - 1);
  }
  appendRoiStructuredLog(roiData);
}

void MainWindowController::onMetadataReceived(const QList<ObjectInfo> &objects)
{
  m_metadataSynchronizer.pushMetadata(objects, QDateTime::currentMSecsSinceEpoch());
}

void MainWindowController::onFrameCaptured(const QImage &frame)
{
  if (!m_ui.videoWidget)
  {
    return;
  }
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_ui.videoWidget->updateMetadata(m_metadataSynchronizer.consumeReady(nowMs));
  m_ui.videoWidget->updateFrame(frame);
}
