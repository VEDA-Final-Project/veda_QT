#include "mainwindow.h"
#include "core/config.h"
#include <QCoreApplication>
#include <QDate>
#include <QDebug>
#include <QDateTime>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QDir>
#include <QtGlobal>
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
  QPushButton *btnDeleteRoi = new QPushButton("ROI 삭제", this);
  refreshRoiSelector();

  btnLayout->addWidget(btnPlay);
  btnLayout->addWidget(btnExit);
  btnLayout->addSpacing(20);
  btnLayout->addWidget(btnApplyRoi);
  btnLayout->addWidget(btnFinishRoi);
  btnLayout->addSpacing(20);
  btnLayout->addWidget(nameLabel);
  btnLayout->addWidget(m_roiNameEdit);
  btnLayout->addSpacing(12);
  btnLayout->addWidget(purposeLabel);
  btnLayout->addWidget(m_roiPurposeCombo);
  btnLayout->addSpacing(12);
  btnLayout->addWidget(roiLabel);
  btnLayout->addWidget(m_roiSelectorCombo);
  btnLayout->addWidget(btnDeleteRoi);
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

  const QString roiDbPath =
      QDir(QCoreApplication::applicationDirPath()).filePath("config/roi.sqlite");
  QString dbError;
  if (!m_roiRepository.init(roiDbPath, &dbError))
  {
    m_logView->append(QString("[ROI][DB] 초기화 실패: %1").arg(dbError));
  }
  else
  {
    m_roiRecords = m_roiRepository.loadAll(&dbError);
    if (!dbError.isEmpty())
    {
      m_logView->append(QString("[ROI][DB] 로드 실패: %1").arg(dbError));
    }
    else
    {
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
      m_videoWidget->queueNormalizedRoiPolygons(normalizedPolygons);
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
        m_logView->append(QString("[ROI][DB] %1개 ROI 로드 완료").arg(m_roiRecords.size()));
      }
    }
  }

  connect(btnPlay, &QPushButton::clicked, this, &MainWindow::playCctv);
  connect(btnExit, &QPushButton::clicked, this, &MainWindow::close);
  connect(btnApplyRoi, &QPushButton::clicked, this, [this]() {
    m_videoWidget->startRoiDrawing();
    m_logView->append("[ROI] Draw mode: left-click points, then press 'ROI 완료'.");
  });
  connect(btnFinishRoi, &QPushButton::clicked, this, [this]() {
    const QString typedName =
        m_roiNameEdit ? m_roiNameEdit->text().trimmed() : QString();
    QString nameError;
    if (!isValidRoiName(typedName, &nameError))
    {
      m_logView->append(QString("[ROI] 완료 실패: %1").arg(nameError));
      return;
    }
    if (isDuplicateRoiName(typedName))
    {
      m_logView->append(
          QString("[ROI] 완료 실패: 이름 '%1' 이(가) 이미 존재합니다.").arg(typedName));
      return;
    }
    if (!m_videoWidget->completeRoiDrawing())
    {
      m_logView->append("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
    }
  });
  connect(btnDeleteRoi, &QPushButton::clicked, this, [this]() {
    if (!m_roiSelectorCombo)
    {
      return;
    }
    const int recordIndex = m_roiSelectorCombo->currentData().toInt();
    if (recordIndex < 0 || recordIndex >= m_roiRecords.size())
    {
      m_logView->append("[ROI] 삭제 실패: ROI를 선택해주세요.");
      return;
    }
    const QString removedId = m_roiRecords[recordIndex]["rod_id"].toString();
    QString dbError;
    if (!m_roiRepository.removeById(removedId, &dbError))
    {
      m_logView->append(QString("[ROI][DB] 삭제 실패: %1").arg(dbError));
      return;
    }
    if (!m_videoWidget->removeRoiAt(recordIndex))
    {
      m_logView->append("[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
      return;
    }
    const QString removedName = m_roiRecords[recordIndex]["rod_name"].toString();
    m_roiRecords.removeAt(recordIndex);
    refreshRoiSelector();
    if (m_roiSelectorCombo)
    {
      int nextRecordIndex = recordIndex;
      if (nextRecordIndex >= m_roiRecords.size())
      {
        nextRecordIndex = m_roiRecords.size() - 1;
      }
      const int comboIndex =
          (nextRecordIndex >= 0) ? m_roiSelectorCombo->findData(nextRecordIndex) : -1;
      m_roiSelectorCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
    }
    m_logView->append(QString("[ROI] 삭제 완료: %1").arg(removedName));
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
          [this](const QPolygon &polygon, const QSize &frameSize) {
            if (frameSize.isEmpty())
            {
              m_logView->append("[ROI] 저장 실패: 프레임 크기가 유효하지 않습니다.");
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
            const int sequence = m_roiSequence;
            const QString rodId = QString("rod-%1").arg(sequence, 3, 10, QLatin1Char('0'));
            const QString typedName =
                m_roiNameEdit ? m_roiNameEdit->text().trimmed() : QString();
            QString nameError;
            if (!isValidRoiName(typedName, &nameError))
            {
              m_logView->append(QString("[ROI] 생성 실패: %1").arg(nameError));
              return;
            }
            if (isDuplicateRoiName(typedName))
            {
              m_logView->append(QString("[ROI] 생성 실패: 이름 '%1' 이(가) 이미 존재합니다.")
                                    .arg(typedName));
              return;
            }
            QJsonObject roiData {
                {"rod_id", rodId},
                {"rod_name", typedName},
                {"rod_enable", true},
                {"rod_purpose", m_roiPurposeCombo ? m_roiPurposeCombo->currentText() : QStringLiteral("일반 주차")},
                {"rod_points", points},
                {"bbox", QJsonObject{
                             {"x", normX(bbox.x())},
                             {"y", normY(bbox.y())},
                             {"w", qBound(0.0, static_cast<double>(bbox.width()) / frameW, 1.0)},
                             {"h", qBound(0.0, static_cast<double>(bbox.height()) / frameH, 1.0)},
                         }},
                {"created_at", ts},
                {"updated_at", ts},
            };
            m_roiRecords.append(roiData);
            QString dbError;
            if (!m_roiRepository.upsert(roiData, &dbError))
            {
              m_roiRecords.removeLast();
              if (m_videoWidget->roiCount() > 0)
              {
                m_videoWidget->removeRoiAt(m_videoWidget->roiCount() - 1);
              }
              m_logView->append(QString("[ROI][DB] 저장 실패: %1").arg(dbError));
              refreshRoiSelector();
              return;
            }
            refreshRoiSelector();
            if (m_roiSelectorCombo)
            {
              m_roiSelectorCombo->setCurrentIndex(m_roiSelectorCombo->count() - 1);
            }
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

void MainWindow::refreshRoiSelector()
{
  if (!m_roiSelectorCombo)
  {
    return;
  }
  m_roiSelectorCombo->clear();
  m_roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);
  for (int i = 0; i < m_roiRecords.size(); ++i)
  {
    const QJsonObject &record = m_roiRecords[i];
    const QString name = record["rod_name"].toString(QString("rod_%1").arg(i + 1));
    const QString purpose = record["rod_purpose"].toString();
    m_roiSelectorCombo->addItem(QString("%1 | %2").arg(name, purpose), i);
  }
}

bool MainWindow::isValidRoiName(const QString &name, QString *errorMessage) const
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

bool MainWindow::isDuplicateRoiName(const QString &name) const
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
