#include "mainwindowcontroller.h"

#include "core/config.h"
#include "ui/video/videowidget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLineEdit>
#include <QStringList>

MainWindowController::MainWindowController(const UiRefs &uiRefs, QObject *parent)
    : QObject(parent), m_ui(uiRefs)
{
  // 컨트롤러가 하위 서비스의 수명을 소유한다.
  // (QObject parent 관계로 MainWindow 종료 시 함께 정리됨)
  m_cameraManager = new CameraManager(this);
  m_ocrCoordinator = new PlateOcrCoordinator(this);

  // 세션 서비스는 "카메라 제어 + 메타데이터 지연 동기화"를 묶는 파사드 역할.
  m_cameraSession.setCameraManager(m_cameraManager);
  m_cameraSession.setDelayMs(Config::instance().defaultDelayMs());

  // ROI DB 로드 -> UI 반영 -> 시그널 연결 순으로 초기화.
  initRoiDb();
  connectSignals();
}

void MainWindowController::shutdown()
{
  QElapsedTimer timer;
  timer.start();

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

  const QString shutdownLog =
      QString("[Shutdown] camera/session stop finished in %1 ms").arg(timer.elapsed());
  qDebug() << shutdownLog;
  if (m_ui.logView)
  {
    m_ui.logView->append(shutdownLog);
  }
}

void MainWindowController::connectSignals()
{
  // UI 이벤트(버튼/위젯) -> Controller 슬롯 연결
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

  // 백엔드 이벤트(Camera/OCR) -> Controller 슬롯 연결
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
  const QString roiDbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/roi.sqlite");
  const RoiService::InitResult initResult = m_roiService.init(roiDbPath);
  if (!initResult.ok)
  {
    if (m_ui.logView)
    {
      m_ui.logView->append(QString("[ROI][DB] 초기화 실패: %1").arg(initResult.error));
    }
    return;
  }

  if (m_ui.videoWidget)
  {
    // DB에는 정규화 좌표(0~1)로 저장되어 있으므로
    // 첫 프레임 렌더 시 실제 픽셀 좌표로 복원하도록 큐에 적재한다.
    QStringList roiLabels;
    const QVector<QJsonObject> &records = m_roiService.records();
    roiLabels.reserve(records.size());
    for (const QJsonObject &record : records)
    {
      roiLabels.append(record["rod_name"].toString().trimmed());
    }
    m_ui.videoWidget->queueNormalizedRoiPolygons(initResult.normalizedPolygons, roiLabels);
  }

  refreshRoiSelector();
  if (m_ui.logView && initResult.loadedCount > 0)
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
  // 실행 중이면 재시작, 아니면 시작 (토글이 아닌 "재생/재연결" 동작)
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
  // 실제 폴리곤 완료는 VideoWidget에서 처리되고,
  // 성공 시 roiPolygonChanged 시그널이 다시 컨트롤러로 올라온다.
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
    // UI에는 이미 방금 그린 ROI가 추가되어 있을 수 있으므로 롤백 처리.
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
  if (m_ui.videoWidget)
  {
    const int recordIndex = m_roiService.count() - 1;
    m_ui.videoWidget->setRoiLabelAt(recordIndex,
                                    createResult.record["rod_name"].toString().trimmed());
  }
  appendRoiStructuredLog(createResult.record);
}

void MainWindowController::onMetadataReceived(const QList<ObjectInfo> &objects)
{
  // 메타데이터는 즉시 렌더하지 않고 타임스탬프와 함께 큐에 넣는다.
  // 프레임 도착 시점에 지연값(delay)을 반영해 꺼내 쓰기 위함.
  m_cameraSession.pushMetadata(objects, QDateTime::currentMSecsSinceEpoch());
}

void MainWindowController::onFrameCaptured(const QImage &frame)
{
  if (!m_ui.videoWidget)
  {
    return;
  }

  // 프레임 갱신 직전에 "현재 시각 기준으로 준비된 메타데이터"만 소비한다.
  // 이렇게 해야 박스/객체 정보와 비디오 프레임이 더 자연스럽게 맞는다.
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  m_ui.videoWidget->updateMetadata(m_cameraSession.consumeReadyMetadata(nowMs));
  m_ui.videoWidget->updateFrame(frame);
}
