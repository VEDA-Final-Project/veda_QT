#ifndef PARKINGREPOSITORY_H
#define PARKINGREPOSITORY_H

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

/**
 * @brief 주차 기록을 SQLite DB에 저장/조회하는 클래스
 *
 * DatabaseContext를 통해 통합 DB(veda.db)의 parking_logs 테이블을 관리합니다.
 */
class ParkingRepository {
public:
  ParkingRepository();

  /**
   * @brief 테이블 초기화 (앱 시작 시 호출)
   */
  bool init(QString *errorMessage = nullptr);

  /**
   * @brief 입차 기록 생성
   * @return 생성된 레코드의 ID (실패 시 -1)
   */
  int insertEntry(const QString &cameraKey, int objectId,
                  const QString &plateNumber, const QString &zoneName,
                  int roiIndex,
                  const QDateTime &entryTime,
                  QString *errorMessage = nullptr);

  /**
   * @brief 출차 기록 업데이트
   */
  bool updateExit(int recordId, const QDateTime &exitTime,
                  QString *errorMessage = nullptr);

  /**
   * @brief 활성 주차 기록의 결제 상태/금액 업데이트
   */
  bool updatePayment(const QString &cameraKey, const QString &plateNumber,
                     int totalAmount, const QString &payStatus,
                     QString *errorMessage = nullptr);

  /**
   * @brief obj_id로 현재 입차 중인 레코드 조회
   */
  QJsonObject findActiveByObjectId(const QString &cameraKey, int objectId,
                                   QString *errorMessage = nullptr) const;

  /**
   * @brief 활성 레코드의 번호판을 obj_id 기준으로 갱신
   */
  bool updateActivePlateByObjectId(const QString &cameraKey, int objectId,
                                   const QString &plateNumber,
                                   QString *errorMessage = nullptr);

  /**
   * @brief 번호판으로 현재 입차 중인 레코드 조회
   * @return 입차 중인 레코드 (없으면 빈 QJsonObject)
   */
  QJsonObject findActiveByPlate(const QString &cameraKey,
                                const QString &plateNumber,
                                QString *errorMessage = nullptr) const;

  /**
   * @brief 최근 N건의 입출차 기록 조회
   */
  QList<QJsonObject> recentLogs(const QString &cameraKey, int limit = 50,
                                QString *errorMessage = nullptr) const;

  /**
   * @brief 번호판으로 기록 검색 (부분 일치)
   */
  QList<QJsonObject> searchByPlate(const QString &cameraKey,
                                   const QString &plate,
                                   QString *errorMessage = nullptr) const;

  /**
   * @brief 특정 레코드의 번호판 수정
   */
  bool updatePlate(const QString &cameraKey, int recordId, const QString &newPlate,
                   QString *errorMessage = nullptr);

  /**
   * @brief 전체 주차 기록 조회
   */
  QList<QJsonObject> getAllLogs(const QString &cameraKey,
                                QString *errorMessage = nullptr) const;

  /**
   * @brief 주차 기록 삭제
   */
  bool deleteLog(const QString &cameraKey, int id,
                 QString *errorMessage = nullptr);

private:
  bool ensureSchema(QString *errorMessage = nullptr);
};

#endif // PARKINGREPOSITORY_H
