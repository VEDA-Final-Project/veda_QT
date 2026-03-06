#ifndef MEDIAREPOSITORY_H
#define MEDIAREPOSITORY_H

#include <QJsonObject>
#include <QString>
#include <QVector>

class MediaRepository {
public:
  MediaRepository();
  bool init(QString *errorMessage = nullptr);

  // 기록 추가
  bool addMediaRecord(const QString &type, const QString &description,
                      const QString &cameraId, const QString &filePath,
                      QString *errorMessage = nullptr);

  // 모든 기록 조회
  QVector<QJsonObject>
  getAllMediaRecords(QString *errorMessage = nullptr) const;

  // 특정 카메라 기록 조회
  QVector<QJsonObject>
  getMediaRecordsByCamera(const QString &cameraId,
                          QString *errorMessage = nullptr) const;

  // 특정 타입 및 채널 조회 (상시 녹화 연속 재생용)
  QVector<QJsonObject>
  getMediaRecordsByTypeAndCamera(const QString &type, const QString &cameraId,
                                 QString *errorMessage = nullptr) const;

  // 특정 기록 삭제
  bool deleteMediaRecord(int id, QString *errorMessage = nullptr);

  // 1시간 초과된 오래된 기록 조회 (자동 삭제용)
  QVector<QJsonObject>
  getOldMediaRecords(int hours = 1, QString *errorMessage = nullptr) const;
};

#endif // MEDIAREPOSITORY_H
