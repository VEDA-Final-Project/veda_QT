#ifndef USERREPOSITORY_H
#define USERREPOSITORY_H

#include <QMap>
#include <QString>

/**
 * @brief 텔레그램 사용자 정보(ChatID <-> 차량번호)를 관리하는 리포지토리
 *
 * DatabaseContext를 통해 통합 DB(veda.db)의 telegram_users 테이블에 접근합니다.
 */
class UserRepository {
public:
  UserRepository();

  /**
   * @brief 테이블 초기화 (앱 시작 시 호출)
   */
  bool init(QString *errorMessage = nullptr);

  /**
   * @brief 사용자 등록 또는 갱신 (Upsert)
   * @param chatId 텔레그램 Chat ID
   * @param plateNumber 차량번호
   */
  bool registerUser(const QString &chatId, const QString &plateNumber,
                    const QString &name = "", const QString &phone = "",
                    QString *errorMessage = nullptr);

  /**
   * @brief 모든 사용자 목록 조회
   * @return ChatID -> PlateNumber 맵
   */
  QMap<QString, QString> getAllUsers(QString *errorMessage = nullptr) const;

  /**
   * @brief 차량번호로 Chat ID 조회
   * @return Chat ID (없으면 빈 문자열)
   */
  QString findChatIdByPlate(const QString &plateNumber) const;

  /**
   * @brief Chat ID로 차량번호 조회
   * @return 차량번호 (없으면 빈 문자열)
   */
  QString findPlateByChatId(const QString &chatId) const;
};

#endif // USERREPOSITORY_H
