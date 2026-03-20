#ifndef APPLICATION_DB_PARKING_PARKINGLOGAPPLICATIONSERVICE_H
#define APPLICATION_DB_PARKING_PARKINGLOGAPPLICATIONSERVICE_H

#include "application/db/common/operationresult.h"
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

class ParkingService;

struct ParkingLogRow {
  int id = 0;
  QString cameraKey;
  int objectId = 0;
  QString plateNumber;
  QString zoneName;
  QString entryTime;
  QString exitTime;
  QString payStatus;
  int displayAmount = 0;
};

class ParkingLogApplicationService : public QObject {
  Q_OBJECT

public:
  struct Context {
    std::function<ParkingService *()> parkingServiceProvider;
    std::function<QVector<ParkingService *>()> allParkingServicesProvider;
    std::function<ParkingService *(const QString &)>
        parkingServiceForCameraKeyProvider;
  };

  explicit ParkingLogApplicationService(const Context &context,
                                        QObject *parent = nullptr);

  QVector<ParkingLogRow> getRecentLogs(int limitPerService = 100) const;
  QVector<ParkingLogRow> searchLogs(const QString &plateKeyword) const;
  OperationResult forcePlate(const QString &cameraKey, int objectId,
                             const QString &plate) const;
  OperationResult updateLogPlate(const QString &cameraKey, int recordId,
                                 const QString &newPlate) const;
  OperationResult deleteLog(const QString &cameraKey, int recordId) const;

private:
  ParkingService *resolveService(const QString &cameraKey) const;
  QVector<ParkingLogRow> toRows(const QVector<QJsonObject> &logs) const;

  Context m_context;
};

#endif // APPLICATION_DB_PARKING_PARKINGLOGAPPLICATIONSERVICE_H
