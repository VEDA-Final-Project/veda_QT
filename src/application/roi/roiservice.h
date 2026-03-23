#ifndef ROISERVICE_H
#define ROISERVICE_H

#include "infrastructure/persistence/roirepository.h"
#include <QJsonObject>
#include <QList>
#include <QPolygon>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <QVector>
#include <optional>

class RoiService {
public:
  struct RoiInitData {
    QList<QPolygonF> normalizedPolygons;
    int loadedCount = 0;
  };

  Result<RoiInitData> init(const QString &cameraKey = QStringLiteral("camera"));
  std::optional<QString> isValidName(const QString &name) const;
  bool isDuplicateName(const QString &name) const;
  Result<QJsonObject> createFromPolygon(const QPolygon &polygon,
                                        const QSize &frameSize,
                                        const QString &name);
  Result<QJsonObject> setZoneEnabled(const QString &zoneId, bool enabled);
  Result<QString> removeAt(int index);
  QString cameraKey() const;

  const QVector<QJsonObject> &records() const;
  int count() const;

private:
  static QList<QPolygonF>
  toNormalizedPolygons(const QVector<QJsonObject> &records);
  int nextAvailableSequence() const;
  void upsertLocalRecord(const QJsonObject &record);

  RoiRepository m_repository;
  QString m_cameraKey = QStringLiteral("camera");
  QVector<QJsonObject> m_records;
};

#endif // ROISERVICE_H
