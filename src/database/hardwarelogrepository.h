#ifndef HARDWARELOGREPOSITORY_H
#define HARDWARELOGREPOSITORY_H

#include <QDateTime>
#include <QString>


/**
 * @brief 하드웨어 제어 이력(HardwareLog)을 관리하는 리포지토리
 *
 * 라즈베리파이 장치(LED, 차단기 등)의 제어 내역을 DB에 기록합니다.
 */
class HardwareLogRepository {
public:
  HardwareLogRepository();

  /**
   * @brief 테이블 초기화 (앱 시작 시 호출)
   */
  bool init(QString *errorMessage = nullptr);

  /**
   * @brief 하드웨어 제어 로그 추가
   * @param zoneId 구역 ID (ROI ID)
   * @param deviceType 장치 유형 ('LED', 'Servo', 'IR')
   * @param action 수행 동작 ('Open', 'Close', 'On', 'Off')
   */
  bool addLog(const QString &zoneId, const QString &deviceType,
              const QString &action, QString *errorMessage = nullptr);
};

#endif // HARDWARELOGREPOSITORY_H
