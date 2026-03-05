#ifndef ROIREPOSITORY_H
#define ROIREPOSITORY_H

#include <QJsonObject>
#include <QString>
#include <QVector>

class RoiRepository {
public:
  RoiRepository();
  ~RoiRepository();

  // 통합 DB 사용 전제 (경로 인자 제거/무시)
  bool init(QString *errorMessage = nullptr);
  QVector<QJsonObject> loadAll(QString *errorMessage = nullptr) const;
  QVector<QJsonObject> loadByCameraKey(const QString &cameraKey,
                                       QString *errorMessage = nullptr) const;
  bool upsert(const QJsonObject &roiData, QString *errorMessage = nullptr);
  bool removeById(const QString &zoneId, QString *errorMessage = nullptr);

private:
  bool ensureSchema(QString *errorMessage = nullptr);
  static bool isValidRoiRecord(const QJsonObject &roiData);
};

#endif // ROIREPOSITORY_H
