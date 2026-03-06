#ifndef ROIREPOSITORY_H
#define ROIREPOSITORY_H
#include "util/result.h"
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>

class RoiRepository {
public:
  RoiRepository();
  ~RoiRepository();

  std::optional<QString> init();
  Result<QVector<QJsonObject>> loadAll() const;
  Result<QVector<QJsonObject>> loadByCameraKey(const QString &cameraKey) const;
  std::optional<QString> upsert(const QJsonObject &roiData);
  std::optional<QString> removeById(const QString &zoneId);

private:
  std::optional<QString> ensureSchema();
  static bool isValidRoiRecord(const QJsonObject &roiData);
};

#endif // ROIREPOSITORY_H
