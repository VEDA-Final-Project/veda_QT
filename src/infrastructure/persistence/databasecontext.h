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

  /**
   * @brief 현재 연결된 DB 파일 경로를 반환
   * @return DB 파일 경로, 연결되지 않았으면 빈 문자열
   */
  static QString databasePath();

  /**
   * @brief 현재 DB를 지정된 파일로 백업
   * @param backupPath 생성할 백업 파일 경로
   * @param errorMessage 오류 메시지 출력용 포인터
   * @return 성공 여부
   */
  static bool backupDatabase(const QString &backupPath,
                             QString *errorMessage = nullptr);

  /**
   * @brief 타임스탬프가 포함된 백업 파일을 생성
   * @param backupDirPath 백업 파일을 저장할 디렉터리
   * @param createdBackupPath 실제 생성된 백업 파일 경로
   * @param errorMessage 오류 메시지 출력용 포인터
   * @return 성공 여부
   */
  static bool createTimestampedBackup(const QString &backupDirPath,
                                      QString *createdBackupPath = nullptr,
                                      QString *errorMessage = nullptr);

private:
  DatabaseContext() = delete; // 인스턴스화 방지
};

#endif // DATABASECONTEXT_H
