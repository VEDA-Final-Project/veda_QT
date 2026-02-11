#ifndef ROISERVICE_H
#define ROISERVICE_H

#include "roi/roirepository.h"
#include <QJsonObject>
#include <QList>
#include <QPolygon>
#include <QPolygonF>
#include <QSize>
#include <QString>
#include <QVector>

class RoiService
{
public:
  struct InitResult
  {
    bool ok = false;
    QString error;
    QList<QPolygonF> normalizedPolygons;
    int loadedCount = 0;
  };

  struct CreateResult
  {
    bool ok = false;
    QString error;
    QJsonObject record;
  };

  struct DeleteResult
  {
    bool ok = false;
    QString error;
    QString removedName;
  };

  InitResult init(const QString &dbPath);
  bool isValidName(const QString &name, QString *errorMessage) const;
  bool isDuplicateName(const QString &name) const;
  CreateResult createFromPolygon(const QPolygon &polygon, const QSize &frameSize,
                                 const QString &name, const QString &purpose);
  DeleteResult removeAt(int index);

  const QVector<QJsonObject> &records() const;
  int count() const;

private:
  static QList<QPolygonF> toNormalizedPolygons(const QVector<QJsonObject> &records);
  void recomputeSequenceFromRecords();

  RoiRepository m_repository;
  int m_roiSequence = 0;
  QVector<QJsonObject> m_records;
};

#endif // ROISERVICE_H
