#include "cctvcontroller.h"

#include "application/roi/roiservice.h"
#include "infrastructure/camera/camerasource.h"
#include "config/config.h"
#include "presentation/widgets/videowidget.h"
#include "presentation/widgets/camerachannelruntime.h"
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPolygon>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QStringList>
#include <algorithm>

namespace {
constexpr int kMaxLiveSlots = 4;
}

CctvController::CctvController(const UiRefs &uiRefs, Context context,
                               QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void CctvController::initializeViewState() { initChannelCards(); }

void CctvController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnApplyRoi) {
    connect(m_ui.btnApplyRoi, &QPushButton::clicked, this,
            &CctvController::onStartRoiDraw);
  }
  if (m_ui.btnFinishRoi) {
    connect(m_ui.btnFinishRoi, &QPushButton::clicked, this,
            &CctvController::onCompleteRoiDraw);
  }
  if (m_ui.btnDeleteRoi) {
    connect(m_ui.btnDeleteRoi, &QPushButton::clicked, this,
            &CctvController::onDeleteSelectedRoi);
  }
  if (m_ui.roiTargetCombo) {
    connect(m_ui.roiTargetCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &CctvController::onRoiTargetChanged);
  }
  for (VideoWidget *videoWidget : m_ui.videoWidgets) {
    if (!videoWidget) {
      continue;
    }
    connect(videoWidget, &VideoWidget::roiChanged, this,
            &CctvController::onRoiChanged);
    connect(videoWidget, &VideoWidget::roiPolygonChanged, this,
            &CctvController::onRoiPolygonChanged);
  }
}

void CctvController::startInitialCctv() {
  QStringList cameraKeys = Config::instance().cameraKeys();
  const int count =
      std::clamp(static_cast<int>(cameraKeys.size()), 1, kMaxLiveSlots);
  m_selectedChannelIndices.clear();
  for (int i = 0; i < count; ++i) {
    m_selectedChannelIndices.append(i);
  }
  m_selectedChannelIndex = 0;
  rebuildLiveLayout();
  if (m_ui.roiTargetCombo) {
    QSignalBlocker blocker(m_ui.roiTargetCombo);
    m_ui.roiTargetCombo->setCurrentIndex(0);
  }
  onRoiTargetChanged(0);
}

void CctvController::onSystemConfigChanged() { ensureChannelSelected(0); }

bool CctvController::handleMousePress(QObject *obj) {
  for (int i = 0; i < 4; ++i) {
    if (obj == m_ui.channelCards[i] || obj == m_ui.thumbnailLabels[i] ||
        obj == m_ui.channelNameLabels[i]) {
      onChannelCardClicked(i);
      return true;
    }
  }
  return false;
}

void CctvController::refreshRoiSelectorForTarget() {
  if (!m_ui.roiSelectorCombo) {
    return;
  }
  m_ui.roiSelectorCombo->clear();
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);

  const RoiService *service = roiServiceForTarget(m_roiTargetIndex);
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

int CctvController::currentRoiTargetIndex() const { return m_roiTargetIndex; }

int CctvController::selectedChannelIndex() const { return m_selectedChannelIndex; }

int CctvController::selectedChannelCount() const {
  return m_selectedChannelIndices.size();
}

int CctvController::primarySelectedChannelIndex() const {
  return m_selectedChannelIndices.isEmpty() ? 0 : m_selectedChannelIndices.first();
}

void CctvController::selectSingleChannel(int index) {
  if (index < 0 || index >= kMaxLiveSlots) {
    return;
  }
  m_selectedChannelIndices.clear();
  m_selectedChannelIndices.append(index);
  m_selectedChannelIndex = index;
  rebuildLiveLayout();
  onRoiTargetChanged(index);
}

void CctvController::onStartRoiDraw() {
  ensureChannelSelected(m_roiTargetIndex);
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTargetIndex);
  if (!targetWidget) {
    return;
  }
  targetWidget->startRoiDrawing();
  appendLog(QString("[ROI] Draw mode (%1): left-click points, then press 'ROI 완료'.")
                .arg(roiTargetLabel(m_roiTargetIndex)));
}

void CctvController::onCompleteRoiDraw() {
  ensureChannelSelected(m_roiTargetIndex);
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTargetIndex);
  RoiService *targetService = roiServiceForTarget(m_roiTargetIndex);
  if (!targetWidget || !targetService) {
    return;
  }
  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();
  if (auto nameError = targetService->isValidName(typedName);
      nameError.has_value()) {
    appendLog(QString("[ROI] 완료 실패: %1").arg(nameError.value()));
    return;
  }
  if (targetService->isDuplicateName(typedName)) {
    appendLog(
        QString("[ROI] 완료 실패: 이름 '%1' 이(가) 이미 존재합니다.")
            .arg(typedName));
    return;
  }

  if (!targetWidget->completeRoiDrawing()) {
    appendLog("[ROI] 완료 실패: 최소 3개 점이 필요합니다.");
  }
}

void CctvController::onDeleteSelectedRoi() {
  ensureChannelSelected(m_roiTargetIndex);
  VideoWidget *targetWidget = videoWidgetForTarget(m_roiTargetIndex);
  RoiService *targetService = roiServiceForTarget(m_roiTargetIndex);
  CameraSource *targetSource =
      m_context.sourceAt ? m_context.sourceAt(m_roiTargetIndex) : nullptr;
  if (!m_ui.roiSelectorCombo || !targetSource || !targetWidget ||
      !targetService) {
    return;
  }
  if (m_ui.roiSelectorCombo->currentIndex() < 0) {
    return;
  }
  const int recordIndex = m_ui.roiSelectorCombo->currentData().toInt();
  if (recordIndex < 0 || recordIndex >= targetService->count()) {
    appendLog("[ROI] 삭제 실패: ROI를 선택해주세요.");
    return;
  }

  const Result<QString> deleteResult = targetService->removeAt(recordIndex);
  if (!deleteResult.isOk()) {
    appendLog(QString("[ROI][DB] 삭제 실패: %1").arg(deleteResult.error));
    return;
  }
  if (!targetWidget->removeRoiAt(recordIndex)) {
    appendLog("[ROI] 삭제 실패: ROI 상태와 목록이 일치하지 않습니다.");
    return;
  }

  targetSource->syncEnabledRoiPolygons();
  refreshRoiSelectorForTarget();
  if (m_context.refreshZoneTable) {
    m_context.refreshZoneTable();
  }

  int nextRecordIndex = recordIndex;
  if (nextRecordIndex >= targetService->count()) {
    nextRecordIndex = targetService->count() - 1;
  }
  const int comboIndex = (nextRecordIndex >= 0)
                             ? m_ui.roiSelectorCombo->findData(nextRecordIndex)
                             : -1;
  m_ui.roiSelectorCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
  appendLog(QString("[ROI] 삭제 완료: %1").arg(deleteResult.data));
  if (m_context.notifyRoiDeleted) {
    m_context.notifyRoiDeleted(deleteResult.data);
  }
}

void CctvController::onRoiChanged(const QRect &roi) {
  const VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  const int cardIndex = m_context.cardIndexForVideoWidget
                            ? m_context.cardIndexForVideoWidget(sourceWidget)
                            : -1;
  const QString channel =
      (cardIndex >= 0) ? QString("Ch%1").arg(cardIndex + 1)
                       : QStringLiteral("Unknown");
  appendLog(QString("[ROI][%1] bbox x:%2 y:%3 w:%4 h:%5")
                .arg(channel)
                .arg(roi.x())
                .arg(roi.y())
                .arg(roi.width())
                .arg(roi.height()));
}

void CctvController::onRoiPolygonChanged(const QPolygon &polygon,
                                         const QSize &frameSize) {
  if (frameSize.isEmpty()) {
    appendLog("[ROI] 저장 실패: 프레임 크기가 유효하지 않습니다.");
    return;
  }

  const QString typedName =
      m_ui.roiNameEdit ? m_ui.roiNameEdit->text().trimmed() : QString();

  VideoWidget *sourceWidget = qobject_cast<VideoWidget *>(sender());
  int targetIndex = m_roiTargetIndex;
  const int cardIndex = m_context.cardIndexForVideoWidget
                            ? m_context.cardIndexForVideoWidget(sourceWidget)
                            : -1;
  if (cardIndex >= 0) {
    targetIndex = cardIndex;
  }

  CameraSource *targetSource =
      m_context.sourceAt ? m_context.sourceAt(targetIndex) : nullptr;
  RoiService *targetService = roiServiceForTarget(targetIndex);
  VideoWidget *targetWidget = videoWidgetForTarget(targetIndex);
  if (!targetSource || !targetService || !targetWidget) {
    return;
  }

  const Result<QJsonObject> createResult =
      targetService->createFromPolygon(polygon, frameSize, typedName);
  if (!createResult.isOk()) {
    if (targetWidget->roiCount() > 0) {
      targetWidget->removeRoiAt(targetWidget->roiCount() - 1);
    }
    appendLog(QString("[ROI][DB] 저장 실패: %1").arg(createResult.error));
    if (targetIndex == m_roiTargetIndex) {
      refreshRoiSelectorForTarget();
    }
    return;
  }

  if (targetIndex == m_roiTargetIndex) {
    refreshRoiSelectorForTarget();
  }
  if (m_context.refreshZoneTable) {
    m_context.refreshZoneTable();
  }
  if (targetIndex == m_roiTargetIndex && m_ui.roiSelectorCombo) {
    m_ui.roiSelectorCombo->setCurrentIndex(m_ui.roiSelectorCombo->count() - 1);
  }
  targetSource->syncEnabledRoiPolygons();
  const int recordIndex = targetService->count() - 1;
  targetWidget->setRoiLabelAt(
      recordIndex, createResult.data["zone_name"].toString().trimmed());
  if (m_context.appendRoiStructuredLog) {
    m_context.appendRoiStructuredLog(createResult.data);
  }
  if (m_context.notifyRoiCreated) {
    m_context.notifyRoiCreated(
        createResult.data["zone_name"].toString().trimmed());
  }
}

void CctvController::onRoiTargetChanged(int index) {
  if (index < 0 || index >= kMaxLiveSlots) {
    m_roiTargetIndex = 0;
  } else {
    m_roiTargetIndex = index;
  }
  refreshRoiSelectorForTarget();
  if (m_context.refreshParkingLogs) {
    m_context.refreshParkingLogs();
  }
  appendLog(QString("[ROI] 편집 대상 변경: %1").arg(roiTargetLabel(m_roiTargetIndex)));
}

void CctvController::onChannelCardClicked(int index) {
  if (index < 0 || index >= kMaxLiveSlots) {
    return;
  }
  m_selectedChannelIndex = index;

  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  const bool isNoSignal = (index >= cameraKeys.size());

  if (m_ui.roiTargetCombo && m_ui.roiTargetCombo->currentIndex() != index) {
    QSignalBlocker blocker(m_ui.roiTargetCombo);
    m_ui.roiTargetCombo->setCurrentIndex(index);
  }
  onRoiTargetChanged(index);

  if (isChannelSelected(index)) {
    if (m_selectedChannelIndices.size() == 1) {
      updateChannelCardSelection();
      return;
    }
    m_selectedChannelIndices.removeAll(index);
    if (m_selectedChannelIndex == index) {
      m_selectedChannelIndex = primarySelectedChannelIndex();
    }
    rebuildLiveLayout();
    appendLog(QString("[Camera] Ch %1 선택 해제").arg(index + 1));
    if (m_context.refreshZoneTable) {
      m_context.refreshZoneTable();
    }
    return;
  }

  m_selectedChannelIndices.append(index);
  rebuildLiveLayout();
  if (isNoSignal) {
    appendLog(QString("[Camera] Ch %1 선택: 신호 없음").arg(index + 1));
  } else {
    CameraSource *newSource =
        m_context.sourceAt ? m_context.sourceAt(index) : nullptr;
    appendLog(QString("[Camera] Ch %1 선택: %2")
                  .arg(index + 1)
                  .arg(newSource ? newSource->cameraKey()
                                 : QStringLiteral("N/A")));
  }
  if (m_context.refreshZoneTable) {
    m_context.refreshZoneTable();
  }
}

void CctvController::initChannelCards() {
  if (!Config::instance().load()) {
    appendLog("Warning: could not reload config; using existing values.");
  }

  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  for (int i = 0; i < 4; ++i) {
    if (!m_ui.channelCards[i]) {
      continue;
    }

    const bool isNoSignal = (i >= cameraKeys.size());
    m_ui.channelCards[i]->setVisible(true);

    if (m_ui.channelNameLabels[i]) {
      m_ui.channelNameLabels[i]->setText(QString("Ch%1").arg(i + 1));
    }

    if (m_ui.thumbnailLabels[i]) {
      m_ui.thumbnailLabels[i]->setPixmap(QPixmap());
      m_ui.thumbnailLabels[i]->setText(isNoSignal ? QStringLiteral("NO SIGNAL")
                                                  : QStringLiteral("STANDBY"));
      if (isNoSignal) {
        m_ui.thumbnailLabels[i]->setStyleSheet(
            "background: #0a0a1a; color: #555; border-radius: 4px; "
            "font-weight: bold; font-size: 10px;");
      }
    }

    if (m_ui.channelStatusDots[i]) {
      m_ui.channelStatusDots[i]->setStyleSheet(
          isNoSignal
              ? "background: #ef4444; border-radius: 5px; border: none;"
              : "background: #10b981; border-radius: 5px; border: none;");
    }
  }

  for (int i = 0; i < kMaxLiveSlots; ++i) {
    CameraChannelRuntime *channel = m_context.channelAt ? m_context.channelAt(i)
                                                        : nullptr;
    if (channel && channel->videoWidget()) {
      channel->videoWidget()->setVisible(false);
    }
  }
}

void CctvController::ensureChannelSelected(int index) {
  if (index < 0 || index >= kMaxLiveSlots || isChannelSelected(index)) {
    return;
  }
  m_selectedChannelIndices.append(index);
  rebuildLiveLayout();
}

void CctvController::rebuildLiveLayout() {
  QVector<int> normalized;
  normalized.reserve(kMaxLiveSlots);
  for (int index : m_selectedChannelIndices) {
    if (index < 0 || index >= kMaxLiveSlots || normalized.contains(index)) {
      continue;
    }
    normalized.append(index);
  }
  if (normalized.isEmpty()) {
    normalized.append(0);
  }
  m_selectedChannelIndices = normalized;

  const LiveLayoutMode mode = liveLayoutMode();
  const int visibleSlotCount = (mode == LiveLayoutMode::Single)
                                   ? 1
                                   : (mode == LiveLayoutMode::Dual ? 2 : 4);
  applyLiveGridLayout(mode);

  for (int slotIndex = 0; slotIndex < kMaxLiveSlots; ++slotIndex) {
    CameraChannelRuntime *channel =
        m_context.channelAt ? m_context.channelAt(slotIndex) : nullptr;
    if (!channel) {
      continue;
    }

    if (slotIndex >= visibleSlotCount) {
      channel->deactivate();
      channel->setReidPanelActive(false);
      continue;
    }

    if (slotIndex < m_selectedChannelIndices.size()) {
      const int cardIndex = m_selectedChannelIndices[slotIndex];
      CameraSource *source =
          m_context.sourceAt ? m_context.sourceAt(cardIndex) : nullptr;
      if (source) {
        channel->activate(source, cardIndex);
      } else {
        channel->selectCardWithoutStream(cardIndex);
      }
    } else {
      channel->deactivate();
      channel->setReidPanelActive(false);
    }
  }

  if (!isChannelSelected(m_selectedChannelIndex)) {
    m_selectedChannelIndex = primarySelectedChannelIndex();
  }
  updateChannelCardSelection();
}

void CctvController::applyLiveGridLayout(LiveLayoutMode mode) {
  if (!m_ui.videoGridLayout) {
    return;
  }

  for (VideoWidget *videoWidget : m_ui.videoWidgets) {
    if (!videoWidget) {
      continue;
    }
    m_ui.videoGridLayout->removeWidget(videoWidget);
    videoWidget->setVisible(false);
  }

  m_ui.videoGridLayout->setRowStretch(0, 1);
  m_ui.videoGridLayout->setRowStretch(1, 1);
  m_ui.videoGridLayout->setColumnStretch(0, 1);
  m_ui.videoGridLayout->setColumnStretch(1, 1);

  switch (mode) {
  case LiveLayoutMode::Single:
    if (m_ui.videoWidgets[0]) {
      m_ui.videoGridLayout->addWidget(m_ui.videoWidgets[0], 0, 0, 2, 2);
    }
    break;
  case LiveLayoutMode::Dual:
    if (m_ui.videoWidgets[0]) {
      m_ui.videoGridLayout->addWidget(m_ui.videoWidgets[0], 0, 0, 2, 1);
    }
    if (m_ui.videoWidgets[1]) {
      m_ui.videoGridLayout->addWidget(m_ui.videoWidgets[1], 0, 1, 2, 1);
    }
    break;
  case LiveLayoutMode::Quad:
    for (int i = 0; i < kMaxLiveSlots; ++i) {
      if (m_ui.videoWidgets[i]) {
        m_ui.videoGridLayout->addWidget(m_ui.videoWidgets[i], i / 2, i % 2);
      }
    }
    break;
  }
}

void CctvController::updateChannelCardSelection() {
  QStringList cameraKeys = Config::instance().cameraKeys();

  for (int i = 0; i < 4; ++i) {
    if (m_ui.channelCards[i]) {
      const bool isSelected = isChannelSelected(i);
      m_ui.channelCards[i]->setProperty("selected", isSelected);
      m_ui.channelCards[i]->style()->unpolish(m_ui.channelCards[i]);
      m_ui.channelCards[i]->style()->polish(m_ui.channelCards[i]);
    }
    if (m_ui.channelStatusDots[i]) {
      const bool hasSignal = i < cameraKeys.size();
      const CameraSource *source = m_context.sourceAt ? m_context.sourceAt(i)
                                                      : nullptr;
      const bool isSelected = isChannelSelected(i);

      QString style;
      if (!hasSignal) {
        style = QStringLiteral(
            "background: #ef4444; border-radius: 5px; border: none;");
      } else if (!source) {
        style = QStringLiteral(
            "background: #666; border-radius: 5px; border: none;");
      } else {
        switch (source->status()) {
        case CameraSource::Status::Live:
          style = isSelected
                      ? QStringLiteral("background: #00e676; border-radius: "
                                       "5px; border: none;")
                      : QStringLiteral("background: #10b981; border-radius: "
                                       "5px; border: none;");
          break;
        case CameraSource::Status::Connecting:
          style = QStringLiteral(
              "background: #f59e0b; border-radius: 5px; border: none;");
          break;
        case CameraSource::Status::Error:
          style = QStringLiteral(
              "background: #ef4444; border-radius: 5px; border: none;");
          break;
        case CameraSource::Status::Stopped:
        default:
          style = QStringLiteral(
              "background: #666; border-radius: 5px; border: none;");
          break;
        }
      }
      m_ui.channelStatusDots[i]->setStyleSheet(style);
    }
  }
}

bool CctvController::isChannelSelected(int index) const {
  return m_selectedChannelIndices.contains(index);
}

CctvController::LiveLayoutMode CctvController::liveLayoutMode() const {
  const int selectedCount = m_selectedChannelIndices.size();
  if (selectedCount <= 1) {
    return LiveLayoutMode::Single;
  }
  if (selectedCount == 2) {
    return LiveLayoutMode::Dual;
  }
  return LiveLayoutMode::Quad;
}

VideoWidget *CctvController::videoWidgetForTarget(int targetIndex) const {
  CameraChannelRuntime *channel =
      m_context.channelForCardIndex ? m_context.channelForCardIndex(targetIndex)
                                    : nullptr;
  return channel ? channel->videoWidget() : nullptr;
}

RoiService *CctvController::roiServiceForTarget(int targetIndex) const {
  CameraSource *source = m_context.sourceAt ? m_context.sourceAt(targetIndex)
                                            : nullptr;
  return source ? source->roiService() : nullptr;
}

QString CctvController::roiTargetLabel(int targetIndex) const {
  return QString("Ch%1").arg(targetIndex + 1);
}

void CctvController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
