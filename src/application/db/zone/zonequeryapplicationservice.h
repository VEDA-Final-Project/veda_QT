#ifndef APPLICATION_DB_ZONE_ZONEQUERYAPPLICATIONSERVICE_H
#define APPLICATION_DB_ZONE_ZONEQUERYAPPLICATIONSERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

struct ZoneRow {
  QString cameraKey;
  QString zoneName;
  QString occupancyLabel;
  QString createdAtDisplay;
};

class ZoneQueryApplicationService : public QObject {
  Q_OBJECT

public:
  struct Context {
    std::function<QVector<QJsonObject>()> allZoneRecordsProvider;
  };

  explicit ZoneQueryApplicationService(const Context &context,
                                       QObject *parent = nullptr);

  QVector<ZoneRow> getAllZones() const;

private:
  Context m_context;
};

#endif // APPLICATION_DB_ZONE_ZONEQUERYAPPLICATIONSERVICE_H
