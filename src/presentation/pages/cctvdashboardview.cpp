#include "cctvdashboardview.h"

#include "presentation/widgets/videowidget.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegion>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QSplitter>
#include <QToolButton>
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
  channelScrollArea->setMinimumWidth(320);
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

  // --- QUICK TOOLS ---

  auto tintIcon = [](const QString &iconPath, const QColor &color,
                     int rotation = 0) -> QIcon {
    QPixmap pixmap(iconPath);
    if (pixmap.isNull())
      return QIcon();

    if (rotation != 0) {
      QTransform t;
      t.rotate(rotation);
      pixmap = pixmap.transformed(t, Qt::SmoothTransformation);
    }

    QPixmap result(pixmap.size());
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawPixmap(0, 0, pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    painter.end();
    return QIcon(result);
  };

  QString btnStyle =
      "QPushButton { background: transparent; border: 1px solid transparent; "
      "border-radius: 4px; } "
      "QPushButton:hover { background: #334155; border: 1px solid transparent; "
      "} "
      "QPushButton:checked { background: #0f172a; border: 1px solid #10b981; }";

  QPushButton *btnOptionToggle = new QPushButton(this);
  btnOptionToggle->setIcon(
      tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/roi.png", QColor("#94A3B8")));
  btnOptionToggle->setIconSize(QSize(30, 30));
  btnOptionToggle->setFixedSize(44, 44);
  btnOptionToggle->setCheckable(true);
  btnOptionToggle->setToolTip(QString::fromUtf8("ROI 상세 설정"));
  btnOptionToggle->setCursor(Qt::PointingHandCursor);
  btnOptionToggle->setStyleSheet(btnStyle);

  QPushButton *btnFilterToggle = new QPushButton(this);
  btnFilterToggle->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/filter.png", QColor("#94A3B8")));
  btnFilterToggle->setIconSize(QSize(30, 30));
  btnFilterToggle->setFixedSize(44, 44);
  btnFilterToggle->setCheckable(true);
  btnFilterToggle->setToolTip(QString::fromUtf8("객체 필터 보기"));
  btnFilterToggle->setCursor(Qt::PointingHandCursor);
  btnFilterToggle->setStyleSheet(btnStyle);

  m_ui.btnCaptureManual = new QPushButton(this);
  m_ui.btnCaptureManual->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/capture.png", QColor("#94A3B8")));
  m_ui.btnCaptureManual->setIconSize(QSize(30, 30));
  m_ui.btnCaptureManual->setFixedSize(44, 44);
  m_ui.btnCaptureManual->setToolTip(QString::fromUtf8("이미지 캡처"));
  m_ui.btnCaptureManual->setCursor(Qt::PointingHandCursor);
  m_ui.btnCaptureManual->setStyleSheet(btnStyle);

  m_ui.btnRecordManual = new QPushButton(this);
  m_ui.btnRecordManual->setIcon(
      tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/rec.png", QColor("#94A3B8")));
  m_ui.btnRecordManual->setIconSize(QSize(30, 30));
  m_ui.btnRecordManual->setFixedSize(44, 44);
  m_ui.btnRecordManual->setCheckable(true);
  m_ui.btnRecordManual->setToolTip(QString::fromUtf8("영상 녹화"));
  m_ui.btnRecordManual->setCursor(Qt::PointingHandCursor);

  QString recBtnStyle = "QPushButton { background: transparent; border: 1px "
                        "solid transparent; border-radius: 4px; } "
                        "QPushButton:hover { background: #334155; border: 1px "
                        "solid transparent; } "
                        "QPushButton:checked { background: #ef4444; border: "
                        "1px solid transparent; }";
  m_ui.btnRecordManual->setStyleSheet(recBtnStyle);

  m_ui.chkShowFps = new QPushButton(this);
  m_ui.chkShowFps->setIcon(
      tintIcon(PROJECT_SOURCE_DIR "/src/ui/icon/fps.png", QColor("#94A3B8")));
  m_ui.chkShowFps->setIconSize(QSize(30, 30));
  m_ui.chkShowFps->setFixedSize(44, 44);
  m_ui.chkShowFps->setCheckable(true);
  m_ui.chkShowFps->setToolTip(QString::fromUtf8("FPS 표시"));
  m_ui.chkShowFps->setCursor(Qt::PointingHandCursor);
  m_ui.chkShowFps->setText(QString());
  m_ui.chkShowFps->setStyleSheet(btnStyle);

  QString groupBoxStyle =
      "QGroupBox { background-color: transparent; border: 1px solid #334155; "
      "border-radius: 6px; margin-top: 10px; padding-top: 8px; }"
      "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top "
      "left; left: 10px; padding: 0 4px; color: #94A3B8; font-size: 11px; "
      "font-weight: bold; }";

  QGroupBox *toolsBox = new QGroupBox(QString::fromUtf8("TOOLS"), this);
  toolsBox->setObjectName("toolsBox");
  toolsBox->setStyleSheet(groupBoxStyle);

  QGridLayout *toolsGrid = new QGridLayout(toolsBox);
  toolsGrid->setContentsMargins(12, 12, 12, 12);
  toolsGrid->setHorizontalSpacing(8);
  toolsGrid->setVerticalSpacing(8);

  toolsGrid->setAlignment(Qt::AlignCenter);

  toolsGrid->addWidget(btnOptionToggle, 0, 0);
  toolsGrid->addWidget(btnFilterToggle, 0, 1);
  toolsGrid->addWidget(m_ui.btnCaptureManual, 0, 2);
  toolsGrid->addWidget(m_ui.btnRecordManual, 0, 3);
  toolsGrid->addWidget(m_ui.chkShowFps, 0, 4);

  channelPanelLayout->addWidget(toolsBox);

  // --- ROI WIDGET (Collapsible) ---
  QFrame *roiWidget = new QFrame(this);
  roiWidget->setObjectName("roiWidget");
  roiWidget->setStyleSheet("QFrame#roiWidget { background-color: #1e293b; "
                           "border: 1px solid #334155; border-radius: 6px; }");
  QVBoxLayout *roiLayout = new QVBoxLayout(roiWidget);
  roiLayout->setContentsMargins(12, 12, 12, 12);
  roiLayout->setSpacing(8);

  // --- ARROW ICON GENERATION ---
  auto saveTintedIcon = [&](const QString &src, const QString &dst) {
    QIcon icon = tintIcon(src, QColor("#94A3B8"));
    if (!icon.isNull()) {
      icon.pixmap(24, 24).save(dst, "PNG");
    }
  };
  saveTintedIcon(PROJECT_SOURCE_DIR "/src/ui/icon/down_arrow.png",
                 PROJECT_SOURCE_DIR "/src/ui/icon/down_arrow_gray.png");
  saveTintedIcon(PROJECT_SOURCE_DIR "/src/ui/icon/up_arrow.png",
                 PROJECT_SOURCE_DIR "/src/ui/icon/up_arrow_gray.png");

  QString comboStyle =
      QString(
          "QComboBox { background-color: #1a1a2e; border: 1px solid #334155; "
          "border-radius: 4px; padding: 4px 32px 4px 12px; color: #e2e8f0; }"
          "QComboBox::drop-down { subcontrol-origin: padding; "
          "subcontrol-position: top right; width: 24px; border: none; "
          "background: transparent; }"
          "QComboBox::down-arrow { image: url(%1); width: 10px; height: 10px; "
          "border: none; background: transparent; }"
          "QComboBox::down-arrow:on { image: url(%2); }")
          .arg(QString::fromUtf8(PROJECT_SOURCE_DIR
                                 "/src/ui/icon/down_arrow_gray.png"))
          .arg(QString::fromUtf8(PROJECT_SOURCE_DIR
                                 "/src/ui/icon/up_arrow_gray.png"));

  QLabel *roiTargetLabel = new QLabel(QString::fromUtf8("ROI 대상:"), this);
  roiTargetLabel->setStyleSheet("color: #ccc; font-size: 11px;");

  m_ui.roiTargetCombo = new QComboBox(roiWidget);
  m_ui.roiTargetCombo->setStyleSheet(comboStyle);
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch1"));
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch2"));
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch3"));
  m_ui.roiTargetCombo->addItem(QStringLiteral("Ch4"));
  roiLayout->addWidget(roiTargetLabel);
  roiLayout->addWidget(m_ui.roiTargetCombo);

  QLabel *roiNameLabel = new QLabel(QString::fromUtf8("이름:"), this);
  roiNameLabel->setStyleSheet("color: #ccc; font-size: 11px;");
  m_ui.roiNameEdit = new QLineEdit(roiWidget);
  m_ui.roiNameEdit->setPlaceholderText(QStringLiteral("ROI 이름 입력(필수)"));
  m_ui.roiNameEdit->setClearButtonEnabled(true);
  m_ui.roiNameEdit->setMaxLength(20);
  m_ui.roiNameEdit->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("^[A-Za-z0-9가-힣 _-]{0,20}$")),
      m_ui.roiNameEdit));
  roiLayout->addWidget(roiNameLabel);
  roiLayout->addWidget(m_ui.roiNameEdit);

  QLabel *roiLabel = new QLabel("ROI:", this);
  roiLabel->setStyleSheet("color: #ccc; font-size: 11px;");
  m_ui.roiSelectorCombo = new QComboBox(roiWidget);
  m_ui.roiSelectorCombo->setStyleSheet(comboStyle);
  m_ui.roiSelectorCombo->setMinimumContentsLength(12);
  m_ui.roiSelectorCombo->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_ui.roiSelectorCombo->addItem(QStringLiteral("ROI 선택"), -1);
  roiLayout->addWidget(roiLabel);
  roiLayout->addWidget(m_ui.roiSelectorCombo);
  roiLayout->addSpacing(4);

  m_ui.btnApplyRoi = new QPushButton(QString::fromUtf8("구역 설정"), roiWidget);
  m_ui.btnFinishRoi = new QPushButton(QString::fromUtf8("ROI 완료"), roiWidget);
  m_ui.btnDeleteRoi = new QPushButton(QString::fromUtf8("ROI 삭제"), roiWidget);
  roiLayout->addWidget(m_ui.btnApplyRoi);
  roiLayout->addWidget(m_ui.btnFinishRoi);
  roiLayout->addWidget(m_ui.btnDeleteRoi);

  roiWidget->setVisible(false);
  channelPanelLayout->addWidget(roiWidget);
  connect(btnOptionToggle, &QPushButton::toggled, roiWidget,
          &QWidget::setVisible);

  // --- FILTER WIDGET (Hidden) ---
  QFrame *filterWidget = new QFrame(this);
  filterWidget->setObjectName("filterWidget");
  filterWidget->setStyleSheet(
      "QFrame#filterWidget { background-color: #1e293b; border: 1px solid "
      "#334155; border-radius: 6px; }");
  QVBoxLayout *filterLayout = new QVBoxLayout(filterWidget);
  filterLayout->setContentsMargins(12, 12, 12, 12);
  filterLayout->setSpacing(8);

  m_ui.chkVehicle = new QCheckBox(QString::fromUtf8("차량"), filterWidget);
  m_ui.chkVehicle->setChecked(true);
  m_ui.chkPlate = new QCheckBox(QString::fromUtf8("번호판"), filterWidget);
  m_ui.chkPlate->setChecked(true);
  filterLayout->addWidget(m_ui.chkVehicle);
  filterLayout->addWidget(m_ui.chkPlate);

  filterWidget->setVisible(false);
  channelPanelLayout->addWidget(filterWidget);
  connect(btnFilterToggle, &QPushButton::toggled, filterWidget,
          &QWidget::setVisible);

  // --- FPS LABEL (Hidden) ---
  m_ui.lblAvgFps =
      new QLabel(QString::fromUtf8("최근 1분 평균 FPS: 0.0"), this);
  m_ui.lblAvgFps->setStyleSheet("color: #94A3B8; font-size: 11px;");
  m_ui.lblAvgFps->setVisible(false);
  channelPanelLayout->addWidget(m_ui.lblAvgFps);
  connect(m_ui.chkShowFps, &QCheckBox::toggled, m_ui.lblAvgFps,
          &QLabel::setVisible);

  // --- CHANNELS ---
  QGroupBox *channelsBox = new QGroupBox(QString::fromUtf8("CHANNELS"), this);
  channelsBox->setObjectName("channelsBox");
  channelsBox->setStyleSheet(groupBoxStyle);

  QVBoxLayout *channelsBoxLayout = new QVBoxLayout(channelsBox);
  channelsBoxLayout->setContentsMargins(8, 12, 8, 8);
  channelsBoxLayout->setSpacing(8);
  channelPanelLayout->addWidget(channelsBox);

  for (int i = 0; i < 4; ++i) {
    QFrame *card = new QFrame(this);
    card->setObjectName(QString("channelCard%1").arg(i));
    card->setProperty("selected", i == 0);
    card->setFixedHeight(120);
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet("QFrame { background: #1a1a2e; border: 2px solid #333; "
                        "border-radius: 8px; }"
                        "QFrame[selected=\"true\"] { border-color: #00e676; }");

    QHBoxLayout *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(6, 6, 6, 6);

    QLabel *thumbnailLabel = new QLabel(card);
    thumbnailLabel->setFixedSize(180, 101);
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

    // Layout configuration with enlarged thumbnail and balanced spacing
    cardLayout->addStretch();
    cardLayout->addWidget(thumbnailLabel);
    cardLayout->addSpacing(12);
    cardLayout->addLayout(infoLayout);
    cardLayout->addStretch();

    m_ui.channelCards[i] = card;
    m_ui.channelNameLabels[i] = nameLabel;
    m_ui.channelStatusDots[i] = statusDot;
    m_ui.thumbnailLabels[i] = thumbnailLabel;
    channelsBoxLayout->addWidget(card);
  }

  // --- PTZ CONTROL GROUP ---
  QGroupBox *ptzBox = new QGroupBox(QString::fromUtf8("PTZ CONTROL"), this);
  ptzBox->setObjectName("ptzBox");
  ptzBox->setStyleSheet(groupBoxStyle);

  QVBoxLayout *ptzMainLayout = new QVBoxLayout(ptzBox);
  ptzMainLayout->setContentsMargins(12, 16, 12, 12);
  ptzMainLayout->setSpacing(20);

  // Directional Controls (D-Pad) - Circular Style
  QFrame *remoteCircularFrame = new QFrame(ptzBox);
  remoteCircularFrame->setFixedSize(160, 160);
  remoteCircularFrame->setStyleSheet(
      "QFrame { background: #1e293b; border: 2px solid #334155; border-radius: "
      "80px; }");
  // Clip all children to circle
  remoteCircularFrame->setMask(QRegion(0, 0, 160, 160, QRegion::Ellipse));

  QGridLayout *dpadLayout = new QGridLayout(remoteCircularFrame);
  dpadLayout->setSpacing(0);
  dpadLayout->setContentsMargins(0, 0, 0, 0);

  auto createRemoteBtn = [&](const QString &iconPath, int rotation = 0) {
    QPushButton *btn = new QPushButton(remoteCircularFrame);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (!iconPath.isEmpty()) {
      btn->setIcon(tintIcon(iconPath, QColor("#94A3B8"), rotation));
      btn->setIconSize(QSize(24, 24));
    }
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(
        "QPushButton { background: transparent; border: none; } "
        "QPushButton:hover { background: rgba(148, 163, 184, 0.12); } "
        "QPushButton:pressed { background: rgba(16, 185, 129, 0.25); }");
    return btn;
  };

  const QString arrowIcon = PROJECT_SOURCE_DIR "/src/ui/icon/up.png";
  m_ui.btnPtzUp = createRemoteBtn(arrowIcon, 0);
  m_ui.btnPtzDown = createRemoteBtn(arrowIcon, 180);
  m_ui.btnPtzLeft = createRemoteBtn(arrowIcon, -90);
  m_ui.btnPtzRight = createRemoteBtn(arrowIcon, 90);

  m_ui.btnPtzReset =
      createRemoteBtn(PROJECT_SOURCE_DIR "/src/ui/icon/reset.png");
  m_ui.btnPtzReset->setFixedSize(40, 40);
  m_ui.btnPtzReset->setIconSize(QSize(24, 24));
  m_ui.btnPtzReset->setStyleSheet(
      "QPushButton { background: #0f172a; border: 1px solid #334155; "
      "border-radius: 20px; } "
      "QPushButton:hover { background: #334155; } "
      "QPushButton:pressed { background: #10b981; }");

  dpadLayout->addWidget(m_ui.btnPtzUp, 0, 1, Qt::AlignCenter);

  dpadLayout->addWidget(m_ui.btnPtzLeft, 1, 0, Qt::AlignCenter);
  dpadLayout->addWidget(m_ui.btnPtzReset, 1, 1, Qt::AlignCenter);
  dpadLayout->addWidget(m_ui.btnPtzRight, 1, 2, Qt::AlignCenter);

  dpadLayout->addWidget(m_ui.btnPtzDown, 2, 1, Qt::AlignCenter);

  QHBoxLayout *circleWrapper = new QHBoxLayout();
  circleWrapper->addStretch();
  circleWrapper->addWidget(remoteCircularFrame);
  circleWrapper->addStretch();
  ptzMainLayout->addLayout(circleWrapper);

  // Zoom Controls - Using individual icons
  QHBoxLayout *zoomLayout = new QHBoxLayout();
  zoomLayout->setSpacing(20);

  m_ui.btnZoomOut =
      createRemoteBtn(PROJECT_SOURCE_DIR "/src/ui/icon/zoom_out.png");
  m_ui.btnZoomIn =
      createRemoteBtn(PROJECT_SOURCE_DIR "/src/ui/icon/zoon_in.png");
  m_ui.btnZoomOut->setFixedSize(44, 44);
  m_ui.btnZoomIn->setFixedSize(44, 44);
  m_ui.btnZoomOut->setStyleSheet(m_ui.btnZoomOut->styleSheet() +
                                 "QPushButton { background: #1e293b; border: "
                                 "1px solid #334155; border-radius: 22px; }");
  m_ui.btnZoomIn->setStyleSheet(m_ui.btnZoomIn->styleSheet() +
                                "QPushButton { background: #1e293b; border: "
                                "1px solid #334155; border-radius: 22px; }");

  zoomLayout->addStretch();
  zoomLayout->addWidget(m_ui.btnZoomOut);
  zoomLayout->addWidget(m_ui.btnZoomIn);
  zoomLayout->addStretch();
  ptzMainLayout->addLayout(zoomLayout);

  channelPanelLayout->addWidget(ptzBox);
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
  connect(btnToggleChannel, &QPushButton::clicked, this,
          [mainSplitter, btnToggleChannel]() {
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
  footerLayout->setContentsMargins(16, 0, 0, 0);

  m_ui.footerTimeLabel = new QLabel(this);
  m_ui.footerTimeLabel->setObjectName("footerTime");
  m_ui.footerTimeLabel->setText(
      QDateTime::currentDateTime().toString("yyyy/MM/dd  HH:mm:ss"));

  m_ui.settingsButton = new QPushButton(this);
  m_ui.settingsButton->setIcon(tintIcon(
      PROJECT_SOURCE_DIR "/src/ui/icon/option.png", QColor("#94A3B8")));
  m_ui.settingsButton->setIconSize(QSize(28, 28));
  m_ui.settingsButton->setFixedSize(40, 40);
  m_ui.settingsButton->setObjectName("btnWindowCtrl");
  m_ui.settingsButton->setCursor(Qt::PointingHandCursor);
  m_ui.settingsButton->setToolTip(QString::fromUtf8("로그 설정"));
  m_ui.settingsButton->setStyleSheet(
      "QPushButton::menu-indicator { image: none; width: 0px; }");

  footerLayout->addWidget(m_ui.footerTimeLabel);
  footerLayout->addStretch();
  footerLayout->addWidget(m_ui.settingsButton);
  cctvLayout->addWidget(footerFrame);
}
