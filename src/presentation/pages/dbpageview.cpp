#include "dbpageview.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

DbPageView::DbPageView(QWidget *parent) : QWidget(parent) { setupUi(); }

const DbUiRefs &DbPageView::uiRefs() const { return m_ui; }

void DbPageView::setupUi() {
  QVBoxLayout *dbLayout = new QVBoxLayout(this);

  m_ui.dbSubTabs = new QTabWidget(this);

  QWidget *logsTab = new QWidget(this);
  QVBoxLayout *logsLayout = new QVBoxLayout(logsTab);
  QHBoxLayout *logsToolBar = new QHBoxLayout();
  m_ui.plateSearchInput = new QLineEdit(this);
  m_ui.plateSearchInput->setPlaceholderText("번호판 정확히 검색...");
  m_ui.btnSearchPlate = new QPushButton("검색", this);
  m_ui.btnRefreshLogs = new QPushButton("새로고침", this);
  logsToolBar->addWidget(m_ui.plateSearchInput);
  logsToolBar->addWidget(m_ui.btnSearchPlate);
  logsToolBar->addWidget(m_ui.btnRefreshLogs);
  logsToolBar->addStretch();

  m_ui.parkingLogTable = new QTableWidget(this);
  m_ui.parkingLogTable->setColumnCount(8);
  m_ui.parkingLogTable->setHorizontalHeaderLabels(
      QStringList() << "ID" << "Object ID" << "번호판" << "구역명" << "입차시간"
                    << "출차시간" << "지불여부" << "총 금액");
  m_ui.parkingLogTable->setColumnHidden(0, true);
  m_ui.parkingLogTable->setColumnHidden(1, true);
  m_ui.parkingLogTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  m_ui.parkingLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_ui.parkingLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  logsLayout->addLayout(logsToolBar);
  logsLayout->addWidget(m_ui.parkingLogTable);
  m_ui.dbSubTabs->addTab(logsTab, "🚗 주차 이력");

  QWidget *usersTab = new QWidget(this);
  QVBoxLayout *usersLayout = new QVBoxLayout(usersTab);
  QHBoxLayout *usersToolBar = new QHBoxLayout();
  m_ui.btnRefreshUsers = new QPushButton("새로고침", this);
  m_ui.btnAddUser = new QPushButton("추가", this);
  m_ui.btnEditUser = new QPushButton("수정", this);
  m_ui.btnDeleteUser = new QPushButton("삭제", this);
  usersToolBar->addWidget(m_ui.btnRefreshUsers);
  usersToolBar->addWidget(m_ui.btnAddUser);
  usersToolBar->addWidget(m_ui.btnEditUser);
  usersToolBar->addWidget(m_ui.btnDeleteUser);
  usersToolBar->addStretch();

  m_ui.userDbTable = new QTableWidget(this);
  m_ui.userDbTable->setColumnCount(6);
  m_ui.userDbTable->setHorizontalHeaderLabels(
      QStringList() << "Chat ID" << "번호판" << "이름" << "연락처" << "카드번호"
                    << "등록일");
  m_ui.userDbTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_ui.userDbTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_ui.userDbTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_ui.userDbTable->setSelectionMode(QAbstractItemView::SingleSelection);
  usersLayout->addLayout(usersToolBar);
  usersLayout->addWidget(m_ui.userDbTable);
  m_ui.dbSubTabs->addTab(usersTab, "👥 사용자");

  QWidget *vhTab = new QWidget(this);
  QVBoxLayout *vhLayout = new QVBoxLayout(vhTab);
  vhLayout->setContentsMargins(0, 0, 0, 0);
  vhLayout->setSpacing(8);

  QLabel *reidSectionTitle = new QLabel(QString::fromUtf8("실시간 객체 정보 / 차량 정보 입력"), this);
  reidSectionTitle->setObjectName("panelTitle");
  vhLayout->addWidget(reidSectionTitle);

  m_ui.reidTable = new QTableWidget(this);
  m_ui.reidTable->setColumnCount(4);
  m_ui.reidTable->setHorizontalHeaderLabels(
      QStringList() << QString::fromUtf8("채널") << "ReID" << "Obj ID"
                    << QString::fromUtf8("번호판"));
  m_ui.reidTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_ui.reidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_ui.reidTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  vhLayout->addWidget(m_ui.reidTable, 1);

  QGroupBox *forceGroup = new QGroupBox(
      QString::fromUtf8("선택 객체 정보 수정 (실험용 상세 제어)"), this);
  QVBoxLayout *forceLayout = new QVBoxLayout();
  QHBoxLayout *labelRow = new QHBoxLayout();
  labelRow->addWidget(new QLabel("Object ID", this), 1);
  labelRow->addWidget(new QLabel("Plate", this), 4);
  labelRow->addWidget(new QLabel("", this), 1);

  QHBoxLayout *inputRow = new QHBoxLayout();
  m_ui.forceObjectIdInput = new QSpinBox(this);
  m_ui.forceObjectIdInput->setRange(0, 2147483647);
  inputRow->addWidget(m_ui.forceObjectIdInput, 1);
  m_ui.forcePlateInput = new QLineEdit(this);
  m_ui.forcePlateInput->setPlaceholderText("Plate");
  inputRow->addWidget(m_ui.forcePlateInput, 4);
  m_ui.btnForcePlate = new QPushButton(QString::fromUtf8("정보 업데이트"), this);
  inputRow->addWidget(m_ui.btnForcePlate, 1);
  forceLayout->addLayout(labelRow);
  forceLayout->addLayout(inputRow);
  forceGroup->setLayout(forceLayout);
  vhLayout->addWidget(forceGroup);

  QGroupBox *settingsGroup =
      new QGroupBox(QString::fromUtf8("표시 및 보존 설정"), this);
  QHBoxLayout *settingsLayout = new QHBoxLayout();
  settingsLayout->addWidget(
      new QLabel(QString::fromUtf8("Stale Timeout (ms):"), this));
  m_ui.staleTimeoutInput = new QSpinBox(this);
  m_ui.staleTimeoutInput->setRange(0, 60000);
  m_ui.staleTimeoutInput->setValue(1000);
  m_ui.staleTimeoutInput->setSingleStep(500);
  settingsLayout->addWidget(m_ui.staleTimeoutInput);
  settingsLayout->addSpacing(20);
  settingsLayout->addWidget(
      new QLabel(QString::fromUtf8("Prune Timeout (ms):"), this));
  m_ui.pruneTimeoutInput = new QSpinBox(this);
  m_ui.pruneTimeoutInput->setRange(0, 315360000);
  m_ui.pruneTimeoutInput->setValue(5000);
  m_ui.pruneTimeoutInput->setSingleStep(1000);
  settingsLayout->addWidget(m_ui.pruneTimeoutInput);
  settingsLayout->addSpacing(20);
  m_ui.chkShowStaleObjects =
      new QCheckBox(QString::fromUtf8("Stale 객체 표시"), this);
  m_ui.chkShowStaleObjects->setChecked(true);
  settingsLayout->addWidget(m_ui.chkShowStaleObjects);
  settingsLayout->addStretch();
  settingsGroup->setLayout(settingsLayout);
  vhLayout->addWidget(settingsGroup);
  m_ui.dbSubTabs->addTab(vhTab, "🚘 차량 정보");

  QWidget *zoneTab = new QWidget(this);
  QVBoxLayout *zoneLayout = new QVBoxLayout(zoneTab);
  QHBoxLayout *zoneToolBar = new QHBoxLayout();
  m_ui.btnRefreshZone = new QPushButton("새로고침", this);
  zoneToolBar->addWidget(m_ui.btnRefreshZone);
  zoneToolBar->addStretch();
  m_ui.zoneTable = new QTableWidget(this);
  m_ui.zoneTable->setColumnCount(4);
  m_ui.zoneTable->setHorizontalHeaderLabels(
      QStringList() << "카메라" << "이름" << "점유" << "생성일");
  m_ui.zoneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_ui.zoneTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_ui.zoneTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  zoneLayout->addLayout(zoneToolBar);
  zoneLayout->addWidget(m_ui.zoneTable);
  m_ui.dbSubTabs->addTab(zoneTab, "📍 주차구역 현황");

  dbLayout->addWidget(m_ui.dbSubTabs);
}
