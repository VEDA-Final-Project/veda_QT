#include "application/db/zone/zonequeryapplicationservice.h"

#include <QDateTime>
#include <QJsonObject>
#include <QTimeZone>

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

QString occupancyLabelForRecord(const QJsonObject &record) {
  if (record.contains("live_occupied")) {
    return record["live_occupied"].toBool()
               ? QStringLiteral("주차중")
               : QStringLiteral("빈자리");
  }

  return record["zone_enable"].toBool(true) ? QStringLiteral("빈자리")
                                            : QStringLiteral("주차중");
}
} // namespace

ZoneQueryApplicationService::ZoneQueryApplicationService(const Context &context,
                                                         QObject *parent)
    : QObject(parent), m_context(context) {}

QVector<ZoneRow> ZoneQueryApplicationService::getAllZones() const {
  const QVector<QJsonObject> allRecords =
      m_context.allZoneRecordsProvider ? m_context.allZoneRecordsProvider()
                                       : QVector<QJsonObject>();

  QVector<ZoneRow> rows;
  rows.reserve(allRecords.size());
  for (const QJsonObject &record : allRecords) {
    rows.append(ZoneRow{
        record["camera_key"].toString(),
        record["zone_name"].toString(),
        occupancyLabelForRecord(record),
        formatDisplayDateTime(record["created_at"].toString()),
    });
  }
  return rows;
}
