#include "telegrampageview.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

TelegramPageView::TelegramPageView(QWidget *parent) : QWidget(parent) {
  setupUi();
}

const TelegramUiRefs &TelegramPageView::uiRefs() const { return m_ui; }

void TelegramPageView::setupUi() {
  QVBoxLayout *tgLayout = new QVBoxLayout(this);

  QGroupBox *statusGroup = new QGroupBox(QString::fromUtf8("상태 정보"), this);
  QFormLayout *statusForm = new QFormLayout();
  m_ui.userCountLabel = new QLabel(QString::fromUtf8("- 명"), this);
  statusForm->addRow(QString::fromUtf8("등록된 사용자 수:"), m_ui.userCountLabel);
  statusGroup->setLayout(statusForm);
  tgLayout->addWidget(statusGroup);

  QGroupBox *entryGroup =
      new QGroupBox(QString::fromUtf8("입차 알림 테스트"), this);
  QHBoxLayout *entryRow = new QHBoxLayout();
  m_ui.entryPlateInput = new QLineEdit(this);
  m_ui.entryPlateInput->setPlaceholderText(
      QString::fromUtf8("차량번호 입력 (예: 123가4567)"));
  m_ui.btnSendEntry = new QPushButton(QString::fromUtf8("입차 알림 전송"), this);
  entryRow->addWidget(new QLabel(QString::fromUtf8("차량번호:"), this));
  entryRow->addWidget(m_ui.entryPlateInput);
  entryRow->addWidget(m_ui.btnSendEntry);
  entryGroup->setLayout(entryRow);
  tgLayout->addWidget(entryGroup);

  QGroupBox *exitGroup =
      new QGroupBox(QString::fromUtf8("출차 알림 테스트"), this);
  QHBoxLayout *exitRow = new QHBoxLayout();
  m_ui.exitPlateInput = new QLineEdit(this);
  m_ui.exitPlateInput->setPlaceholderText(
      QString::fromUtf8("차량번호 입력 (예: 123가4567)"));
  m_ui.feeInput = new QSpinBox(this);
  m_ui.feeInput->setRange(0, 999999);
  m_ui.feeInput->setValue(5000);
  m_ui.feeInput->setSuffix(QString::fromUtf8(" 원"));
  m_ui.feeInput->setSingleStep(1000);
  m_ui.btnSendExit = new QPushButton(QString::fromUtf8("출차 알림 전송"), this);
  exitRow->addWidget(new QLabel(QString::fromUtf8("차량번호:"), this));
  exitRow->addWidget(m_ui.exitPlateInput);
  exitRow->addWidget(new QLabel(QString::fromUtf8("요금:"), this));
  exitRow->addWidget(m_ui.feeInput);
  exitRow->addWidget(m_ui.btnSendExit);
  exitGroup->setLayout(exitRow);
  tgLayout->addWidget(exitGroup);

  QGroupBox *userListGroup =
      new QGroupBox(QString::fromUtf8("등록된 사용자 목록"), this);
  QVBoxLayout *userListLayout = new QVBoxLayout();
  m_ui.userTable = new QTableWidget(this);
  m_ui.userTable->setColumnCount(2);
  m_ui.userTable->setHorizontalHeaderLabels(
      QStringList() << "Chat ID" << QString::fromUtf8("차량번호"));
  m_ui.userTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_ui.userTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_ui.userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  userListLayout->addWidget(m_ui.userTable);
  userListGroup->setLayout(userListLayout);
  tgLayout->addWidget(userListGroup);

  tgLayout->addStretch();
}
