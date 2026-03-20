#include "cctvdashboardview.h"

#include "ui/video/videowidget.h"
#include <QDateTime>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

CctvDashboardView::CctvDashboardView(QWidget *parent) : QWidget(parent) {
  setupUi();
}

const CctvUiRefs &CctvDashboardView::uiRefs() const { return m_ui; }

void CctvDashboardView::setupUi() {
  QVBoxLayout *cctvLayout = new QVBoxLayout(this);
  cctvLayout->setContentsMargins(0, 0, 0, 0);
  cctvLayout->setSpacing(0);

  QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);
  mainSplitter->setHandleWidth(4);
  mainSplitter->setChildrenCollapsible(false);

  QScrollArea *channelScrollArea = new QScrollArea(this);
  channelScrollArea->setObjectName("channelScrollArea");
  channelScrollArea->setWidgetResizable(true);
  channelScrollArea->setMaximumWidth(400);
  channelScrollArea->setFrameShape(QFrame::NoFrame);
  channelScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  channelScrollArea->setStyleSheet(
      "QScrollArea { background: transparent; border: none; }");

  QFrame *channelPanel = new QFrame(channelScrollArea);
  channelPanel->setObjectName("channelPanel");
  channelPanel->setMinimumWidth(0);
  QVBoxLayout *channelPanelLayout = new QVBoxLayout(channelPanel);
  channelPanelLayout->setContentsMargins(12, 12, 4, 12);
  channelPanelLayout->setSpacing(8);

  QLabel *channelTitle = new QLabel(QString::fromUtf8("CHANNELS"), this);
  channelTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(channelTitle);
  channelPanelLayout->addSpacing(4);

  for (int i = 0; i < 4; ++i) {
    QFrame *card = new QFrame(this);
    card->setObjectName(QString("channelCard%1").arg(i));
    card->setProperty("selected", i == 0);
    card->setFixedHeight(90);
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet("QFrame { background: #1a1a2e; border: 2px solid #333; "
                        "border-radius: 8px; }"
                        "QFrame[selected=\"true\"] { border-color: #00e676; }");

    QHBoxLayout *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(10, 8, 10, 8);

    QLabel *thumbnailLabel = new QLabel(card);
    thumbnailLabel->setFixedSize(120, 68);
    thumbnailLabel->setStyleSheet("background: #000; border-radius: 4px;");
    thumbnailLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);

    QLabel *nameLabel = new QLabel(QString("Ch%1").arg(i + 1), card);
    nameLabel->setStyleSheet("color: #ccc; font-size: 13px; font-weight: bold; "
                             "background: transparent; border: none;");

    QLabel *statusDot = new QLabel(card);
    statusDot->setFixedSize(10, 10);
    statusDot->setStyleSheet(
        "background: #10b981; border-radius: 5px; border: none;");

    infoLayout->addWidget(nameLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(statusDot);

    cardLayout->addWidget(thumbnailLabel);
    cardLayout->addSpacing(12);
    cardLayout->addLayout(infoLayout, 1);

    m_ui.channelCards[i] = card;
    m_ui.channelNameLabels[i] = nameLabel;
    m_ui.channelStatusDots[i] = statusDot;
    m_ui.thumbnailLabels[i] = thumbnailLabel;
    channelPanelLayout->addWidget(card);
  }
  channelPanelLayout->addSpacing(12);

  QLabel *roiTitle = new QLabel(QString::fromUtf8("ROI 설정"), this);
  roiTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(roiTitle);
  channelPanelLayout->addSpacing(4);

  QLabel *roiTargetLabel = new QLabel(QString::fromUtf8("ROI 대상"), this);
  m_ui.roiTargetCombo = new QComboBox(this);
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch1"));
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch2"));
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch3"));
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch4"));
  channelPanelLayout->addWidget(roiTargetLabel);
  channelPanelLayout->addWidget(m_ui.roiTargetCombo);

  QLabel *roiNameLabel = new QLabel(QString::fromUtf8("이름"), this);
  m_ui.roiNameEdit = new QLineEdit(this);
  m_ui.roiNameEdit->setPlaceholderText(QStringLiteral("ROI 이름 입력(필수)"));
  m_ui.roiNameEdit->setClearButtonEnabled(true);
  m_ui.roiNameEdit->setMaxLength(20);
  m_ui.roiNameEdit->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("^[A-Za-z0-9가-힣 _-]{0,20}$")),
      m_ui.roiNameEdit));
  channelPanelLayout->addWidget(roiNameLabel);
  channelPanelLayout->addWidget(m_ui.roiNameEdit);

  QLabel *roiLabel = new QLabel("ROI", this);
  m_ui.roiSelectorCombo = new QComboBox(this);
  m_ui.roiSelectorCombo->setMinimumContentsLength(12);
  m_ui.roiSelectorCombo->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);
  channelPanelLayout->addWidget(roiLabel);
  channelPanelLayout->addWidget(m_ui.roiSelectorCombo);
  channelPanelLayout->addSpacing(4);

  m_ui.btnApplyRoi = new QPushButton(QString::fromUtf8("구역 설정"), this);
  m_ui.btnFinishRoi = new QPushButton(QString::fromUtf8("ROI 완료"), this);
  m_ui.btnDeleteRoi = new QPushButton(QString::fromUtf8("ROI 삭제"), this);
  channelPanelLayout->addWidget(m_ui.btnApplyRoi);
  channelPanelLayout->addWidget(m_ui.btnFinishRoi);
  channelPanelLayout->addWidget(m_ui.btnDeleteRoi);
  channelPanelLayout->addSpacing(12);

  QLabel *filterTitle = new QLabel(QString::fromUtf8("OBJECT FILTER"), this);
  filterTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(filterTitle);
  channelPanelLayout->addSpacing(4);

  m_ui.chkVehicle = new QCheckBox(QString::fromUtf8("차량"), this);
  m_ui.chkVehicle->setChecked(true);
  m_ui.chkPerson = new QCheckBox(QString::fromUtf8("사람"), this);
  m_ui.chkPerson->setChecked(true);
  m_ui.chkFace = new QCheckBox(QString::fromUtf8("얼굴"), this);
  m_ui.chkFace->setChecked(true);
  m_ui.chkPlate = new QCheckBox(QString::fromUtf8("번호판"), this);
  m_ui.chkPlate->setChecked(true);
  m_ui.chkOther = new QCheckBox(QString::fromUtf8("기타"), this);
  m_ui.chkOther->setChecked(false);
  channelPanelLayout->addWidget(m_ui.chkVehicle);
  channelPanelLayout->addWidget(m_ui.chkPerson);
  channelPanelLayout->addWidget(m_ui.chkFace);
  channelPanelLayout->addWidget(m_ui.chkPlate);
  channelPanelLayout->addWidget(m_ui.chkOther);
  channelPanelLayout->addSpacing(12);

  QLabel *displayTitle = new QLabel(QString::fromUtf8("DISPLAY"), this);
  displayTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(displayTitle);
  channelPanelLayout->addSpacing(4);

  m_ui.chkShowFps = new QCheckBox(QString::fromUtf8("FPS 표시"), this);
  m_ui.lblAvgFps = new QLabel(QString::fromUtf8("최근 1분 평균 FPS: 0.0"), this);
  channelPanelLayout->addWidget(m_ui.chkShowFps);
  channelPanelLayout->addWidget(m_ui.lblAvgFps);
  channelPanelLayout->addSpacing(12);

  QLabel *mediaTitle = new QLabel(QString::fromUtf8("MEDIA"), this);
  mediaTitle->setObjectName("panelTitle");
  channelPanelLayout->addWidget(mediaTitle);
  channelPanelLayout->addSpacing(4);

  m_ui.btnCaptureManual = new QPushButton(QString::fromUtf8("이미지 캡처"), this);
  m_ui.btnRecordManual = new QPushButton(QString::fromUtf8("영상 녹화"), this);
  m_ui.btnRecordManual->setCheckable(true);
  channelPanelLayout->addWidget(m_ui.btnCaptureManual);
  channelPanelLayout->addWidget(m_ui.btnRecordManual);

  channelPanelLayout->addStretch();
  channelScrollArea->setWidget(channelPanel);

  QWidget *centerPanel = new QWidget(this);
  QVBoxLayout *centerLayout = new QVBoxLayout(centerPanel);
  centerLayout->setContentsMargins(0, 0, 0, 0);
  centerLayout->setSpacing(4);

  QHBoxLayout *toggleBarLayout = new QHBoxLayout();
  toggleBarLayout->setContentsMargins(0, 0, 0, 0);
  toggleBarLayout->setSpacing(0);

  QPushButton *btnToggleChannel =
      new QPushButton(QString::fromUtf8("\xE2\x97\x80"), this);
  btnToggleChannel->setObjectName("btnToggleSidebar");
  btnToggleChannel->setFixedSize(24, 24);
  btnToggleChannel->setCursor(Qt::PointingHandCursor);
  btnToggleChannel->setToolTip(QString::fromUtf8("채널 패널 보이기/숨기기"));
  connect(btnToggleChannel, &QPushButton::clicked, this, [mainSplitter, btnToggleChannel]() {
    QList<int> sizes = mainSplitter->sizes();
    if (sizes[0] > 0) {
      sizes[1] += sizes[0];
      sizes[0] = 0;
      btnToggleChannel->setText(QString::fromUtf8("\xE2\x96\xB6"));
    } else {
      sizes[1] -= 220;
      sizes[0] = 220;
      btnToggleChannel->setText(QString::fromUtf8("\xE2\x97\x80"));
    }
    mainSplitter->setSizes(sizes);
  });

  toggleBarLayout->addWidget(btnToggleChannel);
  toggleBarLayout->addStretch();
  centerLayout->addLayout(toggleBarLayout);

  QWidget *videoGridPanel = new QWidget(this);
  m_ui.videoGridLayout = new QGridLayout(videoGridPanel);
  m_ui.videoGridLayout->setContentsMargins(0, 0, 0, 0);
  m_ui.videoGridLayout->setHorizontalSpacing(4);
  m_ui.videoGridLayout->setVerticalSpacing(4);

  for (int i = 0; i < 4; ++i) {
    m_ui.videoWidgets[i] = new VideoWidget(this);
    m_ui.videoWidgets[i]->setVisible(false);
    m_ui.videoWidgets[i]->setMinimumSize(320, 180);
    m_ui.videoGridLayout->addWidget(m_ui.videoWidgets[i], i / 2, i % 2);
  }
  m_ui.videoGridLayout->setRowStretch(0, 1);
  m_ui.videoGridLayout->setRowStretch(1, 1);
  m_ui.videoGridLayout->setColumnStretch(0, 1);
  m_ui.videoGridLayout->setColumnStretch(1, 1);

  centerLayout->addWidget(videoGridPanel, 1);

  mainSplitter->addWidget(channelScrollArea);
  mainSplitter->addWidget(centerPanel);
  mainSplitter->setCollapsible(0, true);
  mainSplitter->setCollapsible(1, false);
  mainSplitter->setStretchFactor(1, 1);
  mainSplitter->setSizes({220, 1070});
  cctvLayout->addWidget(mainSplitter, 1);

  QFrame *footerFrame = new QFrame(this);
  footerFrame->setObjectName("footerFrame");
  footerFrame->setFixedHeight(36);
  QHBoxLayout *footerLayout = new QHBoxLayout(footerFrame);
  footerLayout->setContentsMargins(16, 4, 16, 4);

  m_ui.footerTimeLabel = new QLabel(this);
  m_ui.footerTimeLabel->setObjectName("footerTime");
  m_ui.footerTimeLabel->setText(
      QDateTime::currentDateTime().toString("yyyy/MM/dd  HH:mm:ss"));

  m_ui.recordingDot = new QLabel(this);
  m_ui.recordingDot->setObjectName("recordingDot");
  m_ui.recordingDot->setFixedSize(10, 10);

  m_ui.footerRecordingLabel =
      new QLabel(QString::fromUtf8("Recording"), this);
  m_ui.footerRecordingLabel->setObjectName("recordingLabel");

  footerLayout->addWidget(m_ui.footerTimeLabel);
  footerLayout->addStretch();
  footerLayout->addWidget(m_ui.recordingDot);
  footerLayout->addSpacing(6);
  footerLayout->addWidget(m_ui.footerRecordingLabel);
  cctvLayout->addWidget(footerFrame);
}
