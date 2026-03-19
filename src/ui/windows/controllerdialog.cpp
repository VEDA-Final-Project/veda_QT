#include "controllerdialog.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ControllerDialog::ControllerDialog(QWidget *parent) : QDialog(parent) {
  setupUi();
  setWindowTitle(QString::fromUtf8("🎮 컨트롤러 팝업"));
  setFixedSize(500, 350);
}

void ControllerDialog::setupUi() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(16, 16, 16, 16);
  mainLayout->setSpacing(16);

  QHBoxLayout *topLayout = new QHBoxLayout();

  QGroupBox *grpJoystick =
      new QGroupBox(QString::fromUtf8("조이스틱 (PTZ)"), this);
  QGridLayout *joyLayout = new QGridLayout(grpJoystick);

  m_btnJoyUp = new QPushButton("UP", this);
  m_btnJoyDown = new QPushButton("DOWN", this);
  m_btnJoyLeft = new QPushButton("LEFT", this);
  m_btnJoyRight = new QPushButton("RIGHT", this);

  auto connectJoyButton = [this](QPushButton *btn, const QString &dir) {
    btn->setFixedSize(60, 60);
    btn->setStyleSheet(
        "QPushButton { font-weight: bold; font-size: 14px; background: #333; "
        "color: white; border-radius: 8px; }"
        "QPushButton:pressed { background: #555; }");
    connect(btn, &QPushButton::pressed, this,
            [this, dir]() { onJoystickPressed(dir); });
    connect(btn, &QPushButton::released, this,
            [this, dir]() { onJoystickReleased(dir); });
  };

  connectJoyButton(m_btnJoyUp, "U");
  connectJoyButton(m_btnJoyDown, "D");
  connectJoyButton(m_btnJoyLeft, "L");
  connectJoyButton(m_btnJoyRight, "R");

  joyLayout->addWidget(m_btnJoyUp, 0, 1);
  joyLayout->addWidget(m_btnJoyLeft, 1, 0);
  joyLayout->addWidget(m_btnJoyRight, 1, 2);
  joyLayout->addWidget(m_btnJoyDown, 2, 1);

  topLayout->addWidget(grpJoystick);

  QGroupBox *grpEncoder =
      new QGroupBox(QString::fromUtf8("로터리 엔코더 (ZOOM)"), this);
  QVBoxLayout *encLayout = new QVBoxLayout(grpEncoder);

  m_btnZoomIn = new QPushButton("ZOOM IN (+1)", this);
  m_btnZoomOut = new QPushButton("ZOOM OUT (-1)", this);
  m_btnZoomReset = new QPushButton("RESET (CLK)", this);

  auto setupEncBtn = [](QPushButton *btn) {
    btn->setFixedHeight(40);
    btn->setStyleSheet(
        "QPushButton { font-weight: bold; background: #2b2b36; color: white; "
        "border-radius: 6px; }"
        "QPushButton:pressed { background: #444; }");
  };
  setupEncBtn(m_btnZoomIn);
  setupEncBtn(m_btnZoomOut);
  setupEncBtn(m_btnZoomReset);

  encLayout->addWidget(m_btnZoomIn);
  encLayout->addWidget(m_btnZoomReset);
  encLayout->addWidget(m_btnZoomOut);
  encLayout->addStretch();

  connect(m_btnZoomIn, &QPushButton::clicked, this,
          [this]() { emit simulatedEncoderRotated(1); });
  connect(m_btnZoomOut, &QPushButton::clicked, this,
          [this]() { emit simulatedEncoderRotated(-1); });
  connect(m_btnZoomReset, &QPushButton::clicked, this,
          [this]() { emit simulatedEncoderClicked(); });

  topLayout->addWidget(grpEncoder);
  mainLayout->addLayout(topLayout);

  QGroupBox *grpButtons = new QGroupBox(QString::fromUtf8("버튼 기능"), this);
  QGridLayout *btnLayout = new QGridLayout(grpButtons);

  auto createBtn = [this](int code, const QString &text) {
    QPushButton *button = new QPushButton(text, this);
    button->setFixedHeight(40);
    button->setStyleSheet(
        "QPushButton { font-weight: bold; background: #2b2b36; color: white; "
        "border-radius: 6px; }"
        "QPushButton:pressed { background: #444; }");
    connect(button, &QPushButton::clicked, this,
            [this, code]() { emit simulatedButtonClicked(code); });
    return button;
  };

  m_btnCh1 = createBtn(288, "CH 1");
  m_btnCh2 = createBtn(289, "CH 2");
  m_btnCh3 = createBtn(290, "CH 3");
  m_btnCh4 = createBtn(291, "CH 4");

  m_btnDbMove = createBtn(292, "DB 이동");
  m_btnDbTab = createBtn(293, "DB 탭전환");
  m_btnCapture = createBtn(294, "화면캡쳐");
  m_btnRecord = createBtn(295, "영상녹화(토글)");

  m_btnRecord->setStyleSheet(
      "QPushButton { font-weight: bold; background: #6b2b2b; color: white; "
      "border-radius: 6px; }"
      "QPushButton:pressed { background: #8b3b3b; }");

  btnLayout->addWidget(m_btnCh1, 0, 0);
  btnLayout->addWidget(m_btnCh2, 0, 1);
  btnLayout->addWidget(m_btnCh3, 0, 2);
  btnLayout->addWidget(m_btnCh4, 0, 3);
  btnLayout->addWidget(m_btnDbMove, 1, 0);
  btnLayout->addWidget(m_btnDbTab, 1, 1);
  btnLayout->addWidget(m_btnCapture, 1, 2);
  btnLayout->addWidget(m_btnRecord, 1, 3);

  mainLayout->addWidget(grpButtons);
}

void ControllerDialog::onJoystickPressed(const QString &dir) {
  emit simulatedJoystickMoved(dir, 1);
}

void ControllerDialog::onJoystickReleased(const QString &dir) {
  emit simulatedJoystickMoved(dir, 0);
}
