#include "recordpageview.h"

#include "presentation/widgets/videowidget.h"
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

RecordPageView::RecordPageView(QWidget *parent) : QWidget(parent) { setupUi(); }

const RecordUiRefs &RecordPageView::uiRefs() const { return m_ui; }

void RecordPageView::setupUi() {
  QVBoxLayout *recordLayout = new QVBoxLayout(this);
  recordLayout->setSpacing(8);

  QHBoxLayout *topControlArea = new QHBoxLayout();
  topControlArea->setSpacing(8);

  QGroupBox *manualGroup =
      new QGroupBox(QString::fromUtf8("수동 캡처 / 녹화 제어"), this);
  QHBoxLayout *manualLayout = new QHBoxLayout(manualGroup);
  m_ui.cmbManualCamera = new QComboBox(this);
  m_ui.cmbManualCamera->addItem(QStringLiteral("Ch1"), 0);
  m_ui.cmbManualCamera->addItem(QStringLiteral("Ch2"), 1);
  m_ui.cmbManualCamera->addItem(QStringLiteral("Ch3"), 2);
  m_ui.cmbManualCamera->addItem(QStringLiteral("Ch4"), 3);
  m_ui.btnCaptureRecordTab =
      new QPushButton(QString::fromUtf8("즉시 캡처"), this);
  m_ui.btnRecordRecordTab =
      new QPushButton(QString::fromUtf8("녹화 시작"), this);
  m_ui.btnCaptureRecordTab->setMinimumHeight(32);
  m_ui.btnRecordRecordTab->setMinimumHeight(32);
  m_ui.cmbManualCamera->setMinimumHeight(32);
  m_ui.btnRecordRecordTab->setCheckable(true);
  manualLayout->addWidget(m_ui.cmbManualCamera);
  manualLayout->addWidget(m_ui.btnCaptureRecordTab);
  manualLayout->addWidget(m_ui.btnRecordRecordTab);
  manualLayout->addStretch();
  topControlArea->addWidget(manualGroup, 1);

  QGroupBox *eventGroup =
      new QGroupBox(QString::fromUtf8("이벤트 구간 저장 테스트"), this);
  QHBoxLayout *eventLayout = new QHBoxLayout(eventGroup);
  m_ui.recordEventTypeInput = new QLineEdit(this);
  m_ui.recordEventTypeInput->setVisible(false);
  eventLayout->addWidget(new QLabel(QString::fromUtf8("저장구간(초):"), this));
  m_ui.recordIntervalSpin = new QSpinBox(this);
  m_ui.recordIntervalSpin->setRange(2, 40);
  m_ui.recordIntervalSpin->setValue(3);
  m_ui.recordIntervalSpin->setMinimumHeight(32);
  eventLayout->addWidget(m_ui.recordIntervalSpin);
  m_ui.btnApplyEventSetting = new QPushButton(QString::fromUtf8("적용"), this);
  m_ui.btnApplyEventSetting->setMinimumHeight(32);
  m_ui.btnApplyEventSetting->setStyleSheet(
      "background: #4B5563; color: white; border-radius: 4px; font-weight: "
      "bold; padding: 0 12px;");
  eventLayout->addWidget(m_ui.btnApplyEventSetting);
  eventLayout->addSpacing(8);
  m_ui.btnTriggerEventRecord =
      new QPushButton(QString::fromUtf8("저장 실행"), this);
  m_ui.btnTriggerEventRecord->setMinimumHeight(32);
  m_ui.btnTriggerEventRecord->setStyleSheet(
      "background: #2563eb; color: white; border-radius: 4px; font-weight: "
      "bold;");
  eventLayout->addWidget(m_ui.btnTriggerEventRecord);
  eventLayout->addStretch();
  topControlArea->addWidget(eventGroup, 1);

  QGroupBox *continuousGroup =
      new QGroupBox(QString::fromUtf8("상시 녹화 제어"), this);
  QHBoxLayout *continuousLayout = new QHBoxLayout(continuousGroup);
  continuousLayout->addWidget(
      new QLabel(QString::fromUtf8("녹화시간(분):"), this));
  m_ui.spinRecordRetention = new QSpinBox(this);
  m_ui.spinRecordRetention->setRange(1, 10080);
  m_ui.spinRecordRetention->setValue(10);
  m_ui.spinRecordRetention->setMinimumHeight(32);
  continuousLayout->addWidget(m_ui.spinRecordRetention);
  m_ui.btnApplyContinuousSetting =
      new QPushButton(QString::fromUtf8("적용"), this);
  m_ui.btnApplyContinuousSetting->setMinimumHeight(32);
  m_ui.btnApplyContinuousSetting->setStyleSheet(
      "background: #4B5563; color: white; border-radius: 4px; font-weight: "
      "bold; padding: 0 12px;");
  continuousLayout->addWidget(m_ui.btnApplyContinuousSetting);
  m_ui.lblContinuousStatus = new QLabel(QString::fromUtf8("녹화 중"), this);
  m_ui.lblContinuousStatus->setStyleSheet(
      "color: #10b981; font-weight: bold; margin-left: 8px;");
  continuousLayout->addWidget(m_ui.lblContinuousStatus);
  continuousLayout->addSpacing(16);
  m_ui.btnViewContinuous = new QPushButton(QString::fromUtf8("상시영상"), this);
  m_ui.btnViewContinuous->setMinimumHeight(32);
  m_ui.btnViewContinuous->setStyleSheet(
      "background: #10b981; color: white; border-radius: 4px; font-weight: "
      "bold; padding: 0 16px;");
  continuousLayout->addWidget(m_ui.btnViewContinuous);
  continuousLayout->addStretch();
  topControlArea->addWidget(continuousGroup, 1);
  recordLayout->addLayout(topControlArea);

  QWidget *listPanel = new QWidget(this);
  QVBoxLayout *listLayout = new QVBoxLayout(listPanel);
  listLayout->setSpacing(4);
  QHBoxLayout *titleRow = new QHBoxLayout();
  m_ui.btnRefreshRecordLogs =
      new QPushButton(QString::fromUtf8("새로고침"), this);
  m_ui.btnDeleteRecordLog =
      new QPushButton(QString::fromUtf8("선택 삭제"), this);
  const QString topBtnStyle =
      "QPushButton { background: #334155; color: #CBD5E1; border: none; "
      "border-radius: 4px; padding: 4px 10px; font-size: 11px; }"
      "QPushButton:hover { background: #475569; color: white; }";
  m_ui.btnRefreshRecordLogs->setStyleSheet(topBtnStyle);
  m_ui.btnDeleteRecordLog->setStyleSheet(topBtnStyle);
  QLabel *tableTitle = new QLabel(QString::fromUtf8("저장된 미디어"), this);
  tableTitle->setStyleSheet("font-weight: bold; font-size: 13px;");
  titleRow->addWidget(tableTitle);
  titleRow->addSpacing(8);
  titleRow->addWidget(m_ui.btnRefreshRecordLogs);
  titleRow->addSpacing(4);
  titleRow->addWidget(m_ui.btnDeleteRecordLog);
  titleRow->addStretch();
  listLayout->addLayout(titleRow);

  m_ui.recordLogTable = new QTableWidget(this);
  m_ui.recordLogTable->setColumnCount(4);
  m_ui.recordLogTable->setHorizontalHeaderLabels(
      QStringList() << QString::fromUtf8("시간") << QString::fromUtf8("유형")
                    << QString::fromUtf8("설명") << QString::fromUtf8("선택"));
  m_ui.recordLogTable->setColumnWidth(0, 135);
  m_ui.recordLogTable->setColumnWidth(1, 70);
  m_ui.recordLogTable->setColumnWidth(3, 40);
  m_ui.recordLogTable->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Stretch);
  m_ui.recordLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_ui.recordLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_ui.recordLogTable->setAlternatingRowColors(true);
  listLayout->addWidget(m_ui.recordLogTable, 1);

  m_ui.recordPreviewPathLabel =
      new QLabel(QString::fromUtf8("선택된 파일: 없음"), this);
  m_ui.recordPreviewPathLabel->setWordWrap(true);
  m_ui.recordPreviewPathLabel->setStyleSheet(
      "font-size: 11px; padding: 4px; background: rgba(255,255,255,0.04); "
      "border-radius: 4px; color: #94A3B8;");
  listLayout->addWidget(m_ui.recordPreviewPathLabel);

  QWidget *previewPanel = new QWidget(this);
  QVBoxLayout *previewLayout = new QVBoxLayout(previewPanel);
  m_ui.recordVideoWidget = new VideoWidget(this);
  m_ui.recordVideoWidget->setMinimumWidth(320);
  m_ui.recordVideoWidget->setMinimumHeight(200);
  previewLayout->addWidget(m_ui.recordVideoWidget, 1);

  QWidget *controlBarWidget = new QWidget(this);
  controlBarWidget->setStyleSheet(
      "QWidget { background: #1A2236; border-radius: 6px; padding: 2px; }");
  QVBoxLayout *controlBarLayout = new QVBoxLayout(controlBarWidget);
  controlBarLayout->setContentsMargins(6, 4, 6, 4);
  controlBarLayout->setSpacing(4);

  m_ui.videoSeekSlider = new QSlider(Qt::Horizontal, this);
  m_ui.videoSeekSlider->setRange(0, 1000);
  m_ui.videoSeekSlider->setValue(0);
  m_ui.videoSeekSlider->setEnabled(false);
  m_ui.videoSeekSlider->setStyleSheet(
      "QSlider::groove:horizontal { background: #334155; height: 6px; "
      "border-radius: 3px; }"
      "QSlider::handle:horizontal { background: #3B82F6; width: 14px; "
      "height: 14px; margin: -4px 0; border-radius: 7px; }"
      "QSlider::sub-page:horizontal { background: #3B82F6; "
      "border-radius: 3px; }");
  controlBarLayout->addWidget(m_ui.videoSeekSlider);

  QHBoxLayout *btnRow = new QHBoxLayout();
  btnRow->setSpacing(6);
  m_ui.btnVideoPlay = new QPushButton(QString::fromUtf8("재생"), this);
  m_ui.btnVideoPause = new QPushButton(QString::fromUtf8("일시정지"), this);
  m_ui.btnVideoStop = new QPushButton(QString::fromUtf8("정지"), this);
  m_ui.videoTimeLabel = new QLabel(QString::fromUtf8("00:00 / 00:00"), this);
  const QString playerBtnStyle =
      "QPushButton { background: #334155; color: #CBD5E1; border: none; "
      "border-radius: 4px; padding: 5px 10px; font-size: 12px; min-width: "
      "70px; }"
      "QPushButton:hover { background: #3B82F6; color: white; }"
      "QPushButton:disabled { background: #1E293B; color: #475569; }";
  m_ui.btnVideoPlay->setStyleSheet(playerBtnStyle);
  m_ui.btnVideoPause->setStyleSheet(playerBtnStyle);
  m_ui.btnVideoStop->setStyleSheet(playerBtnStyle);
  m_ui.btnVideoPlay->setEnabled(false);
  m_ui.btnVideoPause->setEnabled(false);
  m_ui.btnVideoStop->setEnabled(false);
  m_ui.videoTimeLabel->setStyleSheet(
      "color: #94A3B8; font-size: 12px; padding-left: 6px;");
  btnRow->addWidget(m_ui.btnVideoPlay);
  btnRow->addWidget(m_ui.btnVideoPause);
  btnRow->addWidget(m_ui.btnVideoStop);
  btnRow->addStretch();
  btnRow->addWidget(m_ui.videoTimeLabel);
  controlBarLayout->addLayout(btnRow);
  previewLayout->addWidget(controlBarWidget);

  QHBoxLayout *bottomLayout = new QHBoxLayout();
  bottomLayout->setContentsMargins(0, 0, 0, 0);
  bottomLayout->setSpacing(8);
  bottomLayout->addWidget(listPanel, 1);
  bottomLayout->addWidget(previewPanel, 2);
  recordLayout->addLayout(bottomLayout, 1);
}
