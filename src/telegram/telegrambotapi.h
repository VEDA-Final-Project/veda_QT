#ifndef TELEGRAMBOTAPI_H
#define TELEGRAMBOTAPI_H

#include "database/userrepository.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSet>
#include <QTimer>

class TelegramBotAPI : public QObject {
  Q_OBJECT

public:
  explicit TelegramBotAPI(QObject *parent = nullptr);

  /// 봇 토큰이 유효한지 확인
  bool isTokenValid() const { return !m_botToken.isEmpty(); }

  /// 현재 등록된 사용자 수 반환
  int registeredUserCount() const;

  /// 등록된 사용자 목록 반환 (ChatID -> 차량번호)
  QMap<QString, QString> getRegisteredUsers() const { return m_chatToPlate; }

  /// 해당 차량번호로 등록된 사용자에게 입차 알림 전송
  void sendEntryNotice(const QString &plateNumber);

  /// 해당 차량번호로 등록된 사용자에게 출차 + 요금 알림 전송
  void sendExitNotice(const QString &plateNumber, int fee);

  /// 메인 메뉴 키보드 전송
  void sendMainMenu(const QString &chatId);

  /// 시스템에서 사용자 삭제 시 메모리 맵 업데이트
  void removeUser(const QString &chatId);

signals:
  /// 로그 메시지 전달용 시그널
  void logMessage(const QString &msg);

  /// 메시지 전송 결과 (성공/실패, 응답내용)
  void messageSent(bool success, const QString &response);

  /// 등록된 사용자 수가 변경되었을 때 발생 (UI 갱신용)
  void usersUpdated(int count);

  /// [Mock] 결제 확인 시그널 (버튼 클릭 시 발생)
  void paymentConfirmed(const QString &plateNumber, int amount);

  /// 관리자 호출 시그널
  void adminSummoned(const QString &chatId, const QString &name);

private slots:
  /// Long Polling으로 업데이트 가져오기
  void pollUpdates();

private:
  void startPolling();

  /// Telegram sendMessage API 호출
  void sendMessage(const QString &chatId, const QString &text,
                   const QString &replyMarkup = "");

  /// Callback Query 응답 (버튼 클릭 시 로딩 종료)
  void answerCallbackQuery(const QString &callbackQueryId, const QString &text);

  /// API 응답 처리
  void handleReply(QNetworkReply *reply);

  QNetworkAccessManager *m_networkManager;
  QString m_botToken;
  qint64 m_lastUpdateId = 0;
  QTimer *m_pollTimer;

  /// 차량번호 <-> ChatID 매핑
  QMap<QString, QString> m_plateToChat;
  QMap<QString, QString> m_chatToPlate; // 역방향 매핑 (ChatID -> 차량번호)

  /// 차량 등록 진행 중인 ChatID 목록 (상태 관리)
  QSet<QString> m_pendingRegistration;

  /// 사용자 정보 리포지토리 (영속화)
  UserRepository m_userRepository;
};

#endif // TELEGRAMBOTAPI_H
