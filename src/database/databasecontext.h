#ifndef DATABASECONTEXT_H
#define DATABASECONTEXT_H

#include <QSqlDatabase>
#include <QString>


/**
 * @brief 통합 데이터베이스(veda.db) 연결 및 초기화를 담당하는 클래스
 *
 * 이 클래스는 어플리케이션 전역에서 사용될 메인 데이터베이스 설정을 관리합니다.
 * 기본적으로 메인 스레드에서 init()을 호출하여 연결을 수립합니다.
 */
class DatabaseContext {
public:
  static const QString ConnectionName; // "VEDA_DB_CONNECTION"

  /**
   * @brief 데이터베이스 초기화 (앱 시작 시 1회 호출)
   * @param dbPath 생성/연결할 DB 파일 경로 (예: "kafka.db")
   * @param errorMessage 오류 메시지 출력용 포인터
   * @return 성공 여부
   */
  static bool init(const QString &dbPath, QString *errorMessage = nullptr);

  /**
   * @brief 현재 스레드에서 사용할 DB 객체 반환
   * @return QSqlDatabase 객체
   */
  static QSqlDatabase database();

private:
  DatabaseContext() = delete; // 인스턴스화 방지
};

#endif // DATABASECONTEXT_H
