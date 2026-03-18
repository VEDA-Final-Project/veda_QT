#include "zonepanelcontroller.h"

#include <QDateTime>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
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
  const QString amPm =
      (hour24 < 12) ? QStringLiteral("오전") : QStringLiteral("오후");
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

ZonePanelController::ZonePanelController(const UiRefs &uiRefs, Context context,
                                         QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void ZonePanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnRefreshZone) {
    connect(m_ui.btnRefreshZone, &QPushButton::clicked, this,
            &ZonePanelController::refreshZoneTable);
  }
}

void ZonePanelController::refreshZoneTable() {
  if (!m_ui.zoneTable) {
    return;
  }

  const QSignalBlocker blocker(m_ui.zoneTable);
  m_ui.zoneTable->setRowCount(0);

  const QVector<QJsonObject> allRecords =
      m_context.allZoneRecordsProvider ? m_context.allZoneRecordsProvider()
                                       : QVector<QJsonObject>();
  for (const QJsonObject &record : allRecords) {
    const int row = m_ui.zoneTable->rowCount();
    m_ui.zoneTable->insertRow(row);

    m_ui.zoneTable->setItem(
        row, 0, new QTableWidgetItem(record["camera_key"].toString()));
    m_ui.zoneTable->setItem(
        row, 1, new QTableWidgetItem(record["zone_name"].toString()));
    const bool isEmpty = record["zone_enable"].toBool(true);
    m_ui.zoneTable->setItem(
        row, 2,
        new QTableWidgetItem(isEmpty ? QStringLiteral("빈자리")
                                     : QStringLiteral("주차중")));
    m_ui.zoneTable->setItem(
        row, 3,
        new QTableWidgetItem(
            formatDisplayDateTime(record["created_at"].toString())));
  }

  appendLog(QString("주차구역 현황 갱신 완료 (%1건)").arg(allRecords.size()));
}

void ZonePanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
