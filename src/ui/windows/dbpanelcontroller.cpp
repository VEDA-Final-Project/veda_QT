#include "dbpanelcontroller.h"

#include "database/hardwarelogrepository.h"
#include "database/userrepository.h"
#include "database/vehiclerepository.h"
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

void populateParkingTable(QTableWidget *table,
                          const QVector<QJsonObject> &logs) {
  if (!table) {
    return;
  }

  table->setRowCount(0);

  for (int i = 0; i < logs.size(); ++i) {
    const QJsonObject &row = logs[i];
    table->insertRow(i);
    table->setItem(i, 0,
                   new QTableWidgetItem(QString::number(row["id"].toInt())));
    table->setItem(
        i, 1,
        new QTableWidgetItem(QString::number(row["object_id"].toInt())));
    table->setItem(i, 2, new QTableWidgetItem(row["plate_number"].toString()));
    const QString roiDisplay = row["roi_name"].toString().isEmpty()
                                   ? QString::number(row["roi_index"].toInt())
                                   : row["roi_name"].toString();
    table->setItem(i, 3, new QTableWidgetItem(roiDisplay));
    table->setItem(i, 4, new QTableWidgetItem(row["entry_time"].toString()));
    table->setItem(i, 5, new QTableWidgetItem(row["exit_time"].toString()));
  }
}
} // namespace

DbPanelController::DbPanelController(const UiRefs &uiRefs, Context context,
                                     QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void DbPanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnRefreshLogs) {
    connect(m_ui.btnRefreshLogs, &QPushButton::clicked, this,
            &DbPanelController::onRefreshParkingLogs);
  }
  if (m_ui.btnSearchPlate) {
    connect(m_ui.btnSearchPlate, &QPushButton::clicked, this,
            &DbPanelController::onSearchParkingLogs);
  }
  if (m_ui.btnForcePlate) {
    connect(m_ui.btnForcePlate, &QPushButton::clicked, this,
            &DbPanelController::onForcePlate);
  }
  if (m_ui.btnEditPlate) {
    connect(m_ui.btnEditPlate, &QPushButton::clicked, this,
            &DbPanelController::onEditPlate);
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
  if (m_ui.btnRefreshHwLogs) {
    connect(m_ui.btnRefreshHwLogs, &QPushButton::clicked, this,
            &DbPanelController::refreshHwLogs);
  }
  if (m_ui.btnClearHwLogs) {
    connect(m_ui.btnClearHwLogs, &QPushButton::clicked, this,
            &DbPanelController::clearHwLogs);
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
  refreshHwLogs();
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
  ParkingService *service = m_context.parkingServiceProvider
                                ? m_context.parkingServiceProvider()
                                : nullptr;
  if (!service) {
    return;
  }

  const QVector<QJsonObject> logs = service->recentLogs(100);
  populateParkingTable(m_ui.parkingLogTable, logs);
  appendLog(QString("[DB][%1] 전체 새로고침: %2건 표시")
                .arg(service->cameraKey())
                .arg(logs.size()));
}

void DbPanelController::onSearchParkingLogs() {
  if (!m_ui.plateSearchInput) {
    return;
  }

  const QString keyword = m_ui.plateSearchInput->text().trimmed();
  if (keyword.isEmpty()) {
    onRefreshParkingLogs();
    return;
  }

  ParkingService *service = m_context.parkingServiceProvider
                                ? m_context.parkingServiceProvider()
                                : nullptr;
  if (!service) {
    return;
  }

  const QVector<QJsonObject> logs = service->searchByPlate(keyword);
  populateParkingTable(m_ui.parkingLogTable, logs);
  appendLog(QString("[DB][%1] '%2' 검색 결과: %3건")
                .arg(service->cameraKey(), keyword)
                .arg(logs.size()));
}

void DbPanelController::onForcePlate() {
  if (!m_ui.forceObjectIdInput || !m_ui.forceTypeInput ||
      !m_ui.forcePlateInput || !m_ui.forceScoreInput || !m_ui.forceBBoxInput) {
    return;
  }

  const int objectId = m_ui.forceObjectIdInput->value();
  const QString type = m_ui.forceTypeInput->text().trimmed();
  const QString plate = m_ui.forcePlateInput->text().trimmed();
  const double score = m_ui.forceScoreInput->value();
  QString bboxText = m_ui.forceBBoxInput->text().trimmed();

  bboxText.remove("x:");
  bboxText.remove("y:");
  bboxText.remove("w:");
  bboxText.remove("h:");
  const QStringList parts =
      bboxText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

  QRectF bbox(0, 0, 0, 0);
  if (parts.size() >= 4) {
    bbox.setX(parts[0].toDouble());
    bbox.setY(parts[1].toDouble());
    bbox.setWidth(parts[2].toDouble());
    bbox.setHeight(parts[3].toDouble());
  }

  ParkingService *service = m_context.parkingServiceProvider
                                ? m_context.parkingServiceProvider()
                                : nullptr;
  if (!service) {
    return;
  }
  service->forceObjectData(objectId, type, plate, score, bbox);
  appendLog(QString("[DB] 강제 업데이트 요청: ID=%1").arg(objectId));
}

void DbPanelController::onEditPlate() {
  if (!m_ui.parkingLogTable || !m_ui.editPlateInput) {
    return;
  }

  const int currentRow = m_ui.parkingLogTable->currentRow();
  if (currentRow < 0) {
    appendLog("[DB] 수정할 레코드를 먼저 선택해주세요.");
    return;
  }

  const QString newPlate = m_ui.editPlateInput->text().trimmed();
  if (newPlate.isEmpty()) {
    appendLog("[DB] 새 번호판을 입력해주세요.");
    return;
  }

  QTableWidgetItem *idItem = m_ui.parkingLogTable->item(currentRow, 0);
  if (!idItem) {
    return;
  }

  const int recordId = idItem->text().toInt();
  ParkingService *service = m_context.parkingServiceProvider
                                ? m_context.parkingServiceProvider()
                                : nullptr;
  if (service && service->updatePlate(recordId, newPlate)) {
    appendLog(QString("[DB] 번호판 수정 완료: ID=%1 → %2")
                  .arg(recordId)
                  .arg(newPlate));
    onRefreshParkingLogs();
    return;
  }
  appendLog(QString("[DB] 번호판 수정 실패: ID=%1").arg(recordId));
}

void DbPanelController::deleteParkingLog() {
  if (!m_ui.parkingLogTable) {
    return;
  }

  const int row = m_ui.parkingLogTable->currentRow();
  if (row < 0) {
    return;
  }

  QTableWidgetItem *idItem = m_ui.parkingLogTable->item(row, 0);
  if (!idItem) {
    return;
  }
  const int id = idItem->text().toInt();

  ParkingService *service = m_context.parkingServiceProvider
                                ? m_context.parkingServiceProvider()
                                : nullptr;
  QString error;
  if (service && service->deleteLog(id, &error)) {
    appendLog(QString("[DB][%1] 주차 기록 삭제 완료: ID=%2")
                  .arg(service->cameraKey())
                  .arg(id));
    onRefreshParkingLogs();
    return;
  }

  const QString reason = error.isEmpty() ? QStringLiteral("unknown") : error;
  appendLog(
      QString("[DB] 주차 기록 삭제 실패: ID=%1 (%2)").arg(id).arg(reason));
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

void DbPanelController::refreshHwLogs() {
  if (!m_ui.hwLogTable) {
    return;
  }

  HardwareLogRepository repo;
  QString error;
  const QVector<QJsonObject> logs = repo.getAllLogs(&error);

  m_ui.hwLogTable->setRowCount(0);
  for (int i = 0; i < logs.size(); ++i) {
    const QJsonObject &row = logs[i];
    m_ui.hwLogTable->insertRow(i);
    m_ui.hwLogTable->setItem(
        i, 0, new QTableWidgetItem(QString::number(row["log_id"].toInt())));
    m_ui.hwLogTable->setItem(i, 1,
                             new QTableWidgetItem(row["zone_id"].toString()));
    m_ui.hwLogTable->setItem(
        i, 2, new QTableWidgetItem(row["device_type"].toString()));
    m_ui.hwLogTable->setItem(i, 3,
                             new QTableWidgetItem(row["action"].toString()));
    m_ui.hwLogTable->setItem(i, 4,
                             new QTableWidgetItem(row["timestamp"].toString()));
  }
}

void DbPanelController::clearHwLogs() {
  HardwareLogRepository repo;
  QString error;
  if (repo.clearLogs(&error)) {
    appendLog("[DB] 장치 로그 초기화 완료");
    refreshHwLogs();
    return;
  }
  appendLog(QString("[DB] 장치 로그 초기화 실패: %1").arg(error));
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

  const QVector<QJsonObject> primaryRecords =
      m_context.primaryZoneRecordsProvider
          ? m_context.primaryZoneRecordsProvider()
          : QVector<QJsonObject>();
  const QVector<QJsonObject> secondaryRecords =
      m_context.secondaryZoneRecordsProvider
          ? m_context.secondaryZoneRecordsProvider()
          : QVector<QJsonObject>();
  appendRows(primaryRecords);
  appendRows(secondaryRecords);

  appendLog(QString("주차구역 현황 갱신 완료 (%1건)")
                .arg(primaryRecords.size() + secondaryRecords.size()));
}
