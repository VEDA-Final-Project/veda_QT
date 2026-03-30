#ifndef VEHICLEREPOSITORY_H
#define VEHICLEREPOSITORY_H

#include <QJsonObject>
#include <QString>
#include <QVector>

/**
 * @brief 차량 정보(Vehicles)를 관리하는 리포지토리
 *
 * 차량의 고유 정보(차종, 색상, 지정주차 여부 등)를 관리합니다.
 */
class VehicleRepository {
public:
  VehicleRepository();

  bool init(QString *errorMessage = nullptr);

  /**
   * @brief 번호판/REID 기준으로 차량을 찾아 없으면 생성하고 vehicle_id를 반환
   */
  int ensureVehicle(const QString &plateNumber, const QString &carType,
                    const QString &carColor,
                    const QString &reidId = QString(),
                    QString *errorMessage = nullptr);

  int findVehicleIdByPlate(const QString &plateNumber,
                           QString *errorMessage = nullptr) const;
  int findVehicleIdByReid(const QString &reidId,
                          QString *errorMessage = nullptr) const;

  /**
   * @brief 차량 정보 추가 또는 업데이트
   */
  bool upsertVehicle(const QString &plateNumber, const QString &carType,
                     const QString &carColor, bool isAssigned,
                     const QString &reidId = QString(),
                     QString *errorMessage = nullptr);

  /**
   * @brief 차량 정보 조회
   */
  QJsonObject findByPlate(const QString &plateNumber,
                          QString *errorMessage = nullptr) const;

  /**
   * @brief 모든 차량 정보 조회
   */
  QVector<QJsonObject> getAllVehicles(QString *errorMessage = nullptr) const;

  /**
   * @brief 차량 정보 삭제
   */
  bool deleteVehicle(const QString &plateNumber,
                     QString *errorMessage = nullptr);
};

#endif // VEHICLEREPOSITORY_H
