#ifndef PARKINGREPOSITORY_H
#define PARKINGREPOSITORY_H

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVector>

/**
 * @brief 주차 기록을 SQLite DB에 저장/조회하는 클래스
 *
 * RoiRepository와 동일한 패턴으로 설계되었습니다.
 * parking_logs 테이블에 입출차 기록을 관리합니다.
 */
class ParkingRepository {
public:
  ParkingRepository();
  ~ParkingRepository();

  /**
   * @brief DB 초기화 및 스키마 생성
   * @param dbFilePath SQLite 파일 경로
   */
  bool init(const QString &dbFilePath, QString *errorMessage = nullptr);

  /**
   * @brief 입차 기록 생성
   * @return 생성된 레코드의 ID (실패 시 -1)
   */
  int insertEntry(const QString &plateNumber, int roiIndex,
                  const QDateTime &entryTime, QString *errorMessage = nullptr);

  /**
   * @brief 출차 기록 업데이트
   */
  bool updateExit(int recordId, const QDateTime &exitTime,
                  QString *errorMessage = nullptr);

  /**
   * @brief 번호판으로 현재 입차 중인 레코드 조회
   * @return 입차 중인 레코드 (없으면 빈 QJsonObject)
   */
  QJsonObject findActiveByPlate(const QString &plateNumber,
                                QString *errorMessage = nullptr) const;

  /**
   * @brief 최근 N건의 입출차 기록 조회
   */
  QVector<QJsonObject> recentLogs(int limit = 50,
                                  QString *errorMessage = nullptr) const;

  /**
   * @brief 번호판으로 기록 검색 (부분 일치)
   */
  QVector<QJsonObject> searchByPlate(const QString &plate,
                                     QString *errorMessage = nullptr) const;

  /**
   * @brief 특정 레코드의 번호판 수정
   */
  bool updatePlate(int recordId, const QString &newPlate,
                   QString *errorMessage = nullptr);

private:
  bool ensureSchema(QString *errorMessage = nullptr);
  QString m_connectionName;
};

#endif // PARKINGREPOSITORY_H
