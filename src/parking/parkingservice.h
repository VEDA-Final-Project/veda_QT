#ifndef PARKINGSERVICE_H
#define PARKINGSERVICE_H

#include "parking/parkingrepository.h"
#include "parking/vehicletracker.h"
#include <QObject>
#include <QString>
#include <QStringList>

class TelegramBotAPI;

/**
 * @brief 주차 관리 시스템의 중앙 비즈니스 로직 서비스
 *
 * 차량 추적(VehicleTracker), DB 관리(ParkingRepository),
 * 알림(TelegramBotAPI)을 통합하여 입출차 이벤트를 처리합니다.
 *
 * MainWindowController는 이 서비스에 메타데이터와 OCR 결과를
 * 전달하고, 결과 시그널을 받아 UI에 반영하기만 하면 됩니다.
 */
class ParkingService : public QObject {
  Q_OBJECT

public:
  explicit ParkingService(QObject *parent = nullptr);

  /**
   * @brief 서비스 초기화 (DB 열기, 외부 의존성 연결)
   */
  bool init(QString *errorMessage = nullptr);

  /**
   * @brief 외부 텔레그램 API 연결
   */
  void setTelegramApi(TelegramBotAPI *api);
  void setCameraKey(const QString &cameraKey);
  QString cameraKey() const;

  /**
   * @brief ROI 폴리곤 목록 갱신 (MainWindowController에서 호출)
   */
  void updateRoiPolygons(const QList<QPolygonF> &polygons,
                         const QStringList &zoneNames = QStringList());

  /**
   * @brief 새 메타데이터 프레임 처리 (입출차 판단의 진입점)
   */
  void processMetadata(const QList<ObjectInfo> &objects, int cropOffsetX,
                       int effectiveWidth, int sourceHeight,
                       qint64 pruneTimeoutMs = 5000);

  /**
   * @brief OCR 시작 수신 처리 (인식 중 상태 표시)
   */
  void processOcrStarted(int objectId);

  /**
   * @brief OCR 결과 수신 처리
   */
  void processOcrResult(int objectId, const QString &plateNumber);


  /**
   * @brief 최근 입출차 기록 조회 (UI 표시용)
   */
  QVector<QJsonObject> recentLogs(int limit = 50) const;

  /**
   * @brief 번호판으로 기록 검색 (UI 표시용)
   */
  QVector<QJsonObject> searchByPlate(const QString &plate) const;

  /**
   * @brief 특정 레코드의 번호판 수정
   */
  bool updatePlate(int recordId, const QString &newPlate);
  bool deleteLog(int recordId, QString *errorMessage = nullptr);
  bool updatePayment(const QString &plateNumber, int totalAmount,
                     const QString &payStatus = QStringLiteral("결제완료"),
                     QString *errorMessage = nullptr);

  /**
   * @brief 수동 번호판 및 객체 정보 강제 지정 (실험용 상세 제어)
   */
  void forceObjectData(int objectId, const QString &type, const QString &plate,
                       double score, const QRectF &bbox);

  /**
   * @brief 특정 객체의 현재 추적 상태 반환 (UI 표시용)
   */
  VehicleState getVehicleState(int objectId) const;

  /**
   * @brief 현재 추적 중인 모든 객체의 상태 목록 반환
   */
  QList<VehicleState> activeVehicles() const;

signals:
  /**
   * @brief 차량 입차 이벤트 발생 시 (UI 업데이트용)
   */
  void vehicleEntered(int roiIndex, const QString &plateNumber);

  /**
   * @brief 차량 출차 이벤트 발생 시
   */
  void vehicleDeparted(int roiIndex, const QString &plateNumber);

  /**
   * @brief 로그 메시지 (UI 로그 창에 표시)
   */
  void logMessage(const QString &msg);

private:
  void handleNewEntry(const VehicleState &vs);
  void handleDeparture(const VehicleState &vs);
  QString zoneNameForIndex(int roiIndex) const;

  VehicleTracker m_tracker;
  ParkingRepository m_repository;
  QString m_cameraKey = QStringLiteral("camera");
  QStringList m_roiZoneNames;
  TelegramBotAPI *m_telegram = nullptr;
};

#endif // PARKINGSERVICE_H
