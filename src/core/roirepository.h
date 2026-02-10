#ifndef ROIREPOSITORY_H
#define ROIREPOSITORY_H

#include <QJsonObject>
#include <QString>
#include <QVector>

class RoiRepository
{
public:
  RoiRepository();
  ~RoiRepository();

  bool init(const QString &dbFilePath, QString *errorMessage = nullptr);
  QVector<QJsonObject> loadAll(QString *errorMessage = nullptr) const;
  bool upsert(const QJsonObject &roiData, QString *errorMessage = nullptr);
  bool removeById(const QString &rodId, QString *errorMessage = nullptr);

private:
  bool ensureSchema(QString *errorMessage = nullptr);
  static bool isValidRoiRecord(const QJsonObject &roiData);

  QString m_connectionName;
};

#endif // ROIREPOSITORY_H
