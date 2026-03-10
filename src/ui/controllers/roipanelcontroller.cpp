#include "roipanelcontroller.h"
#include "camerachannelruntime.h"
#include "roi/roiservice.h"
#include "ui/video/videowidget.h"
#include <QComboBox>
#include <QDebug>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>

RoiPanelController::RoiPanelController(const UiRefs &ui, Context ctx,
                                       QObject *parent)
    : QObject(parent), m_ui(ui), m_ctx(std::move(ctx)) {}

void RoiPanelController::connectSignals() {
  if (m_ui.btnApplyRoi) {
    connect(m_ui.btnApplyRoi, &QPushButton::clicked, this,
            &RoiPanelController::onStartRoiDraw);
  }
  if (m_ui.btnFinishRoi) {
    connect(m_ui.btnFinishRoi, &QPushButton::clicked, this,
            &RoiPanelController::onCompleteRoiDraw);
  }
  if (m_ui.btnDeleteRoi) {
    connect(m_ui.btnDeleteRoi, &QPushButton::clicked, this,
            &RoiPanelController::onDeleteSelectedRoi);
  }
  if (m_ui.roiTargetCombo) {
    connect(m_ui.roiTargetCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RoiPanelController::onRoiTargetChanged);
  }
  if (m_ui.videoWidgetPrimary) {
    connect(m_ui.videoWidgetPrimary, &VideoWidget::roiChanged, this,
            &RoiPanelController::onRoiChanged);
    connect(m_ui.videoWidgetPrimary, &VideoWidget::roiPolygonChanged, this,
            &RoiPanelController::onRoiPolygonChanged);
  }
  if (m_ui.videoWidgetSecondary) {
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiChanged, this,
            &RoiPanelController::onRoiChanged);
    connect(m_ui.videoWidgetSecondary, &VideoWidget::roiPolygonChanged, this,
            &RoiPanelController::onRoiPolygonChanged);
  }
}

int RoiPanelController::roiTarget() const { return m_roiTarget; }

void RoiPanelController::refreshSelector() {
  if (!m_ui.roiSelectorCombo) {
    return;
  }
  m_ui.roiSelectorCombo->clear();
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

  const RoiService *service = roiServiceForTarget();
  if (!service) {
    return;
  }

  const QVector<QJsonObject> &records = service->records();
  for (int i = 0; i < records.size(); ++i) {
    const QJsonObject &record = records[i];
    const QString name =
        record["zone_name"].toString(QString("zone_%1").arg(i + 1));
    m_ui.roiSelectorCombo->addItem(name, i);
  }
}

VideoWidget *RoiPanelController::videoWidgetForTarget() const {
  CameraChannelRuntime *channel = m_ctx.channelAt(m_roiTarget);
  return channel ? channel->videoWidget() : nullptr;
}

RoiService *RoiPanelController::roiServiceForTarget() const {
  return roiServiceAt(m_roiTarget);
}

RoiService *RoiPanelController::roiServiceAt(int target) const {
  CameraChannelRuntime *channel = m_ctx.channelAt(target);
  return channel ? channel->roiService() : nullptr;
}

void RoiPanelController::appendStructuredLog(const QJsonObject &roiData) {
  if (!m_ui.logView) {
    return;
  }
  const QString line =
      QString::fromUtf8(QJsonDocument(roiData).toJson(QJsonDocument::Compact));
  qDebug().noquote() << line;
  m_ui.logView->append(line);
}

void RoiPanelController::onStartRoiDraw() {
  VideoWidget *targetWidget = videoWidgetForTarget();
  if (!targetWidget || !m_ui.logView) {
    return;
  }
  targetWidget->startRoiDrawing();
  m_ui.logView->append(
      QString("[ROI] Draw mode (%1): left-click points, then press 'ROI 완료'.")
          .arg(m_roiTarget == 0 ? "카메라 A" : "카메라 B"));
}

void RoiPanelController::onCompleteRoiDraw() {
  VideoWidget *targetWidget = videoWidgetForTarget();
  RoiService *targetService = roiServiceForTarget();
  if (!targetWidget || !targetService || !m_ui.logView) {
    return;
  }
  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  if (auto nameError = targetService->isValidName(typedName);
      nameError.has_value()) {
    m_ui.logView->append(QString("[ROI] 완료 실패: %1").arg(nameError.value()));
    return;
  }
  if (targetService->isDuplicateName(typedName)) {
    m_ui.logView->append(
        QString("[ROI] 완료 실패: 이름 '%1' 이(가) 이미 존재합니다.")
            .arg(typedName));
    return;
  }

  if (!targetWidget->completeRoiDrawing()) {
    m_ui.logView->append("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
  }
}

void RoiPanelController::onDeleteSelectedRoi() {
  CameraChannelRuntime *targetChannel = m_ctx.channelAt(m_roiTarget);
  VideoWidget *targetWidget = videoWidgetForTarget();
  RoiService *targetService = roiServiceForTarget();
  if (!m_ui.roiSelectorCombo || !targetChannel || !targetWidget ||
      !targetService || !m_ui.logView) {
    return;
  }
  if (m_ui.roiSelectorCombo->currentIndex() < 0) {
    return;
  }
  const int recordIndex = m_ui.roiSelectorCombo->currentData().toInt();
  if (recordIndex < 0 || recordIndex >= targetService->count()) {
    m_ui.logView->append("[ROI] 삭제 실패: ROI를 선택해주세요.");
    return;
  }

  const Result<QString> deleteResult = targetService->removeAt(recordIndex);
  if (!deleteResult.isOk()) {
    m_ui.logView->append(
        QString("[ROI][DB] 삭제 실패: %1").arg(deleteResult.error));
    return;
  }
  if (!targetWidget->removeRoiAt(recordIndex)) {
    m_ui.logView->append(
        "[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
    return;
  }

  targetChannel->syncEnabledRoiPolygons();
  refreshSelector();
  if (m_ctx.refreshZoneTable) {
    m_ctx.refreshZoneTable();
  }

  int nextRecordIndex = recordIndex;
  if (nextRecordIndex >= targetService->count()) {
    nextRecordIndex = targetService->count() - 1;
  }
  const int comboIndex = (nextRecordIndex >= 0)
                             ? m_ui.roiSelectorCombo->findData(nextRecordIndex)
                             : -1;
  m_ui.roiSelectorCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
  m_ui.logView->append(QString("[ROI] 삭제 완료: %1").arg(deleteResult.data));
}

void RoiPanelController::onRoiChanged(const QRect &roi) {
  if (!m_ui.logView) {
    return;
  }
  const VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  const QString channel = (sourceWidget == m_ui.videoWidgetSecondary)
                              ? QStringLiteral("B")
                              : QStringLiteral("A");
  m_ui.logView->append(QString("[ROI][%1] bbox x:%2 y:%3 w:%4 h:%5")
                           .arg(channel)
                           .arg(roi.x())
                           .arg(roi.y())
                           .arg(roi.width())
                           .arg(roi.height()));
}

void RoiPanelController::onRoiPolygonChanged(const QPolygon &polygon,
                                             const QSize &frameSize) {
  if (!m_ui.logView) {
    return;
  }
  if (frameSize.isEmpty()) {
    m_ui.logView->append("[ROI] 저장 실패: 프레임 크기가 유효하지 않습니다.");
    return;
  }

  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();

  VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  int target = m_roiTarget;
  if (sourceWidget == m_ui.videoWidgetSecondary) {
    target = 1;
  } else if (sourceWidget == m_ui.videoWidgetPrimary) {
    target = 0;
  }

  CameraChannelRuntime *targetChannel = m_ctx.channelAt(target);
  if (!targetChannel) {
    return;
  }
  RoiService *targetService = targetChannel->roiService();
  VideoWidget *targetWidget = targetChannel->videoWidget();
  if (!targetService || !targetWidget) {
    return;
  }

  const Result<QJsonObject> createResult =
      targetService->createFromPolygon(polygon, frameSize, typedName);
  if (!createResult.isOk()) {
    if (targetWidget->roiCount() > 0) {
      targetWidget->removeRoiAt(targetWidget->roiCount() - 1);
    }
    m_ui.logView->append(
        QString("[ROI][DB] 저장 실패: %1").arg(createResult.error));
    if (target == m_roiTarget) {
      refreshSelector();
    }
    return;
  }

  if (target == m_roiTarget) {
    refreshSelector();
  }
  if (m_ctx.refreshZoneTable) {
    m_ctx.refreshZoneTable();
  }
  if (target == m_roiTarget && m_ui.roiSelectorCombo) {
    m_ui.roiSelectorCombo->setCurrentIndex(m_ui.roiSelectorCombo->count() - 1);
  }
  targetChannel->syncEnabledRoiPolygons();
  const int recordIndex = targetService->count() - 1;
  targetWidget->setRoiLabelAt(
      recordIndex, createResult.data["zone_name"].toString().trimmed());
  appendStructuredLog(createResult.data);
}

void RoiPanelController::onRoiTargetChanged(int index) {
  m_roiTarget = (index == 1) ? 1 : 0;
  refreshSelector();
  emit roiTargetChanged(m_roiTarget);
  emit logMessage(QString("[ROI] 편집 대상 변경: %1")
                      .arg(m_roiTarget == 0 ? "카메라 A" : "카메라 B"));
}
