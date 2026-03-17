#include "dbpanelcontroller.h"

#include "database/userrepository.h"
#include "database/vehiclerepository.h"
#include "parkinglogpanelcontroller.h"
#include "parking/parkingservice.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRectF>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimeZone>
#include <utility>

namespace {
QString formatDisplayDateTime(const QString &rawIsoText) {
  QDateTime dt = QDateTime::fromString(rawIsoText, Qt::ISODateWithMs);
  if (!dt.isValid()) {
    dt = QDateTime::fromString(rawIsoText, Qt::ISODate);
  }
  if (!dt.isValid()) {
    return rawIsoText;
  }

  const QTimeZone seoulTz("Asia/Seoul");
  if (seoulTz.isValid()) {
    dt = dt.toTimeZone(seoulTz);
  } else {
    dt = dt.toLocalTime();
  }

  const int hour24 = dt.time().hour();
  const QString amPm = (hour24 < 12) ? QStringLiteral("오전")
                                     : QStringLiteral("오후");
  int hour12 = hour24 % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }

  return QStringLiteral("%1년 %2월 %3일 %4 %5시 %6분")
      .arg(dt.date().year())
      .arg(dt.date().month())
      .arg(dt.date().day())
      .arg(amPm)
      .arg(hour12)
      .arg(dt.time().minute(), 2, 10, QLatin1Char('0'));
}
} // namespace

DbPanelController::DbPanelController(const UiRefs &uiRefs, Context context,
                                     QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {
  ParkingLogPanelController::UiRefs parkingUiRefs;
  parkingUiRefs.parkingLogTable = m_ui.parkingLogTable;
  parkingUiRefs.plateSearchInput = m_ui.plateSearchInput;
  parkingUiRefs.btnSearchPlate = m_ui.btnSearchPlate;
  parkingUiRefs.btnRefreshLogs = m_ui.btnRefreshLogs;
  parkingUiRefs.forcePlateInput = m_ui.forcePlateInput;
  parkingUiRefs.forceObjectIdInput = m_ui.forceObjectIdInput;
  parkingUiRefs.btnForcePlate = m_ui.btnForcePlate;
  parkingUiRefs.editPlateInput = m_ui.editPlateInput;
  parkingUiRefs.btnEditPlate = m_ui.btnEditPlate;

  ParkingLogPanelController::Context parkingContext;
  parkingContext.parkingServiceProvider = m_context.parkingServiceProvider;
  parkingContext.allParkingServicesProvider =
      m_context.allParkingServicesProvider;
  parkingContext.parkingServiceForCameraKeyProvider =
      m_context.parkingServiceForCameraKeyProvider;
  parkingContext.logMessage = m_context.logMessage;

  m_parkingLogPanelController =
      new ParkingLogPanelController(parkingUiRefs, parkingContext, this);
}

void DbPanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->connectSignals();
  }
  if (m_ui.btnRefreshUsers) {
    connect(m_ui.btnRefreshUsers, &QPushButton::clicked, this,
            &DbPanelController::refreshUserTable);
  }
  if (m_ui.btnAddUser) {
    connect(m_ui.btnAddUser, &QPushButton::clicked, this,
            &DbPanelController::addUser);
  }
  if (m_ui.btnEditUser) {
    connect(m_ui.btnEditUser, &QPushButton::clicked, this,
            &DbPanelController::editUser);
  }
  if (m_ui.btnDeleteUser) {
    connect(m_ui.btnDeleteUser, &QPushButton::clicked, this,
            &DbPanelController::deleteUser);
  }
  if (m_ui.btnRefreshVehicles) {
    connect(m_ui.btnRefreshVehicles, &QPushButton::clicked, this,
            &DbPanelController::refreshVehicleTable);
  }
  if (m_ui.btnDeleteVehicle) {
    connect(m_ui.btnDeleteVehicle, &QPushButton::clicked, this,
            &DbPanelController::deleteVehicle);
  }
  if (m_ui.btnRefreshZone) {
    connect(m_ui.btnRefreshZone, &QPushButton::clicked, this,
            &DbPanelController::refreshZoneTable);
  }
}

void DbPanelController::refreshAll() {
  onRefreshParkingLogs();
  refreshUserTable();
  refreshVehicleTable();
  refreshZoneTable();
}

void DbPanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
    return;
  }
  if (m_ui.logView) {
    m_ui.logView->append(message);
  }
}

void DbPanelController::onRefreshParkingLogs() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onRefreshParkingLogs();
  }
}

void DbPanelController::onSearchParkingLogs() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onSearchParkingLogs();
  }
}

void DbPanelController::onForcePlate() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onForcePlate();
  }
}

void DbPanelController::onEditPlate() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->onEditPlate();
  }
}

void DbPanelController::deleteParkingLog() {
  if (m_parkingLogPanelController) {
    m_parkingLogPanelController->deleteParkingLog();
  }
}

void DbPanelController::refreshUserTable() {
  if (!m_ui.userDbTable) {
    return;
  }

  UserRepository repo;
  QString error;
  const QVector<QJsonObject> users = repo.getAllUsersFull(&error);

  m_ui.userDbTable->setRowCount(0);
  for (int i = 0; i < users.size(); ++i) {
    const QJsonObject &user = users[i];
    m_ui.userDbTable->insertRow(i);
    m_ui.userDbTable->setItem(i, 0,
                              new QTableWidgetItem(user["chat_id"].toString()));
    m_ui.userDbTable->setItem(
        i, 1, new QTableWidgetItem(user["plate_number"].toString()));
    m_ui.userDbTable->setItem(i, 2,
                              new QTableWidgetItem(user["name"].toString()));
    m_ui.userDbTable->setItem(i, 3,
                              new QTableWidgetItem(user["phone"].toString()));
    m_ui.userDbTable->setItem(
        i, 4, new QTableWidgetItem(user["payment_info"].toString()));
    m_ui.userDbTable->setItem(
        i, 5, new QTableWidgetItem(user["created_at"].toString()));
  }
}

void DbPanelController::deleteUser() {
  if (!m_ui.userDbTable) {
    return;
  }
  const int row = m_ui.userDbTable->currentRow();
  if (row < 0) {
    return;
  }

  QTableWidgetItem *chatIdItem = m_ui.userDbTable->item(row, 0);
  if (!chatIdItem) {
    return;
  }
  const QString chatId = chatIdItem->text();

  UserRepository repo;
  QString error;
  if (repo.deleteUser(chatId, &error)) {
    appendLog(QString("[DB] 사용자 삭제 완료: ChatID=%1").arg(chatId));
    if (m_context.userDeleted) {
      m_context.userDeleted(chatId);
    }
    refreshUserTable();
    return;
  }
  appendLog(QString("[DB] 사용자 삭제 실패: %1").arg(error));
}

// ── 사용자 추가 다이얼로그 ──
void DbPanelController::addUser() {
  if (!m_ui.userDbTable)
    return;

  QDialog dlg;
  dlg.setWindowTitle(QString::fromUtf8("사용자 추가"));
  dlg.setFixedSize(360, 280);

  QFormLayout *form = new QFormLayout(&dlg);
  QLineEdit *eChatId = new QLineEdit;
  QLineEdit *ePlate = new QLineEdit;
  QLineEdit *eName = new QLineEdit;
  QLineEdit *ePhone = new QLineEdit;
  QLineEdit *eCard = new QLineEdit;

  form->addRow(QString::fromUtf8("Chat ID:"), eChatId);
  form->addRow(QString::fromUtf8("번호판:"), ePlate);
  form->addRow(QString::fromUtf8("이름:"), eName);
  form->addRow(QString::fromUtf8("연락처:"), ePhone);
  form->addRow(QString::fromUtf8("카드번호:"), eCard);

  QDialogButtonBox *btns =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form->addRow(btns);
  connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  const QString chatId = eChatId->text().trimmed();
  const QString plate = ePlate->text().trimmed();
  if (chatId.isEmpty() || plate.isEmpty()) {
    appendLog("[DB] 사용자 추가 실패: Chat ID와 번호판은 필수입니다.");
    return;
  }

  UserRepository repo;
  QString error;
  if (repo.addUser(chatId, plate, eName->text().trimmed(),
                   ePhone->text().trimmed(), eCard->text().trimmed(), &error)) {
    appendLog(QString("[DB] 사용자 추가 완료: ChatID=%1").arg(chatId));
    refreshUserTable();
  } else {
    appendLog(QString("[DB] 사용자 추가 실패: %1").arg(error));
  }
}

// ── 사용자 수정 다이얼로그 ──
void DbPanelController::editUser() {
  if (!m_ui.userDbTable)
    return;
  const int row = m_ui.userDbTable->currentRow();
  if (row < 0) {
    appendLog("[DB] 수정할 사용자를 먼저 선택해 주세요.");
    return;
  }

  auto cell = [&](int col) {
    QTableWidgetItem *it = m_ui.userDbTable->item(row, col);
    return it ? it->text() : QString();
  };

  const QString chatId = cell(0);

  QDialog dlg;
  dlg.setWindowTitle(QString::fromUtf8("사용자 수정"));
  dlg.setFixedSize(360, 280);

  QFormLayout *form = new QFormLayout(&dlg);
  QLineEdit *ePlate = new QLineEdit(cell(1));
  QLineEdit *eName = new QLineEdit(cell(2));
  QLineEdit *ePhone = new QLineEdit(cell(3));
  QLineEdit *eCard = new QLineEdit(cell(4));

  form->addRow(QString::fromUtf8("번호판:"), ePlate);
  form->addRow(QString::fromUtf8("이름:"), eName);
  form->addRow(QString::fromUtf8("연락처:"), ePhone);
  form->addRow(QString::fromUtf8("카드번호:"), eCard);

  QDialogButtonBox *btns =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  form->addRow(btns);
  connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  UserRepository repo;
  QString error;
  if (repo.updateUser(chatId, ePlate->text().trimmed(), eName->text().trimmed(),
                      ePhone->text().trimmed(), eCard->text().trimmed(),
                      &error)) {
    appendLog(QString("[DB] 사용자 수정 완료: ChatID=%1").arg(chatId));
    refreshUserTable();
  } else {
    appendLog(QString("[DB] 사용자 수정 실패: %1").arg(error));
  }
}

void DbPanelController::refreshVehicleTable() {
  if (!m_ui.vehicleTable) {
    return;
  }

  VehicleRepository repo;
  QString error;
  const QVector<QJsonObject> vehicles = repo.getAllVehicles(&error);

  m_ui.vehicleTable->setRowCount(0);
  for (int i = 0; i < vehicles.size(); ++i) {
    const QJsonObject &row = vehicles[i];
    m_ui.vehicleTable->insertRow(i);
    m_ui.vehicleTable->setItem(
        i, 0, new QTableWidgetItem(row["plate_number"].toString()));
    m_ui.vehicleTable->setItem(
        i, 1, new QTableWidgetItem(row["car_type"].toString()));
    m_ui.vehicleTable->setItem(
        i, 2, new QTableWidgetItem(row["car_color"].toString()));
    m_ui.vehicleTable->setItem(
        i, 3, new QTableWidgetItem(row["is_assigned"].toBool() ? "Yes" : "No"));
    m_ui.vehicleTable->setItem(
        i, 4, new QTableWidgetItem(row["updated_at"].toString()));
  }
}

void DbPanelController::deleteVehicle() {
  if (!m_ui.vehicleTable) {
    return;
  }

  const int row = m_ui.vehicleTable->currentRow();
  if (row < 0) {
    return;
  }

  QTableWidgetItem *plateItem = m_ui.vehicleTable->item(row, 0);
  if (!plateItem) {
    return;
  }
  const QString plate = plateItem->text();

  VehicleRepository repo;
  QString error;
  if (repo.deleteVehicle(plate, &error)) {
    appendLog(QString("[DB] 차량 정보 삭제 완료: %1").arg(plate));
    refreshVehicleTable();
    return;
  }
  appendLog(QString("[DB] 차량 정보 삭제 실패: %1").arg(error));
}

void DbPanelController::refreshZoneTable() {
  if (!m_ui.zoneTable) {
    return;
  }

  const QSignalBlocker blocker(m_ui.zoneTable);
  m_ui.zoneTable->setRowCount(0);
  auto appendRows = [this](const QVector<QJsonObject> &records) {
    for (const QJsonObject &record : records) {
      const int row = m_ui.zoneTable->rowCount();
      m_ui.zoneTable->insertRow(row);

      m_ui.zoneTable->setItem(
          row, 0, new QTableWidgetItem(record["camera_key"].toString()));
      m_ui.zoneTable->setItem(
          row, 1, new QTableWidgetItem(record["zone_id"].toString()));
      m_ui.zoneTable->setItem(
          row, 2, new QTableWidgetItem(record["zone_name"].toString()));
      const bool isEmpty = record["zone_enable"].toBool(true);
      m_ui.zoneTable->setItem(
          row, 3,
          new QTableWidgetItem(isEmpty ? QStringLiteral("빈자리")
                                       : QStringLiteral("주차중")));
      m_ui.zoneTable->setItem(
          row, 4, new QTableWidgetItem(
                      formatDisplayDateTime(record["created_at"].toString())));
    }
  };

  const QVector<QJsonObject> allRecords =
      m_context.allZoneRecordsProvider ? m_context.allZoneRecordsProvider()
                                       : QVector<QJsonObject>();
  appendRows(allRecords);

  appendLog(QString("주차구역 현황 갱신 완료 (%1건)").arg(allRecords.size()));
}
