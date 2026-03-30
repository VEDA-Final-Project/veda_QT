#ifndef TELEGRAMBOTAPI_H
#define TELEGRAMBOTAPI_H

#include "infrastructure/persistence/userrepository.h"
#include "infrastructure/persistence/parkingrepository.h"
#include "infrastructure/ocr/recognition/telegramllmrunner.h"
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
  void shutdown();

  /// 봇 토큰이 유효한지 확인
  bool isTokenValid() const { return !m_botToken.isEmpty(); }

  /// 현재 등록된 사용자 수 반환
  int registeredUserCount() const;

  /// 등록된 사용자 목록 반환 (ChatID -> 차량번호)
  QMap<QString, QString> getRegisteredUsers() const { return m_chatToPlate; }

  /// 해당 차량번호로 등록된 사용자에게 입차 알림 전송
  void sendEntryNotice(const QString &plateNumber);

  /// 해당 차량번호로 등록된 사용자에게 출차 + 요금 알림 전송
  void sendExitNotice(const QString &plateNumber, int fee,
                      int paymentRecordId = -1);

  /// 메인 메뉴 키보드 전송 (선택적으로 커스텀 메시지 문구 함께 전송)
  void sendMainMenu(const QString &chatId,
                    const QString &customMessage = QString());

  /// 시스템에서 사용자 삭제 시 메모리 맵 업데이트
  void removeUser(const QString &chatId);

private:
  /// 카카오페이 송금 링크 생성 (금액 포함)
  QString generateKakaoPayLink(int amount) const;

signals:
  /// 로그 메시지 전달용 시그널
  void logMessage(const QString &msg);

  /// 메시지 전송 결과 (성공/실패, 응답내용)
  void messageSent(bool success, const QString &response);

  /// 등록된 사용자 수가 변경되었을 때 발생 (UI 갱신용)
  void usersUpdated(int count);

  /// [Mock] 결제 확인 시그널 (버튼 클릭 시 발생)
  void paymentConfirmed(int recordId, const QString &plateNumber, int amount);

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

  // LLM 알림 및 처리 핸들러
  void handleLlmCommand(const QString &chatId, const QString &userText,
                        ocr::recognition::TelegramLlmRunner::CommandCategory category);
  void handleInfoInquiry(const QString &chatId);
  void handleFeeInquiry(const QString &chatId);
  void handleUsageHistory(const QString &chatId);
  void handleCallAdmin(const QString &chatId);
  void handleFeePayment(const QString &chatId);


  QNetworkAccessManager *m_networkManager;
  QString m_botToken;
  qint64 m_lastUpdateId = 0;
  QTimer *m_pollTimer;
  bool m_shuttingDown = false;

  /// 차량번호 <-> ChatID 매핑
  QMap<QString, QString> m_plateToChat;
  QMap<QString, QString> m_chatToPlate; // 역방향 매핑 (ChatID -> 차량번호)

  /// 차량 등록 진행 상태
  enum class RegistrationState {
    IDLE,
    WAIT_PLATE, // 차량번호 입력 대기
    WAIT_PHONE, // 전화번호(연락처 공유) 대기
    WAIT_CARD   // 카드번호 입력 대기
  };

  struct RegistrationData {
    RegistrationState state = RegistrationState::IDLE;
    QString plate;
    QString phone;
    QString name;
  };

  /// 차량 등록 진행 중인 세션 관리 (ChatID -> 등록 데이터)
  QMap<QString, RegistrationData> m_registrationSessions;

  /// 정보 수정 필드 상태
  enum class EditField { NONE, NAME, PLATE, PHONE, CARD };

  struct EditSession {
    EditField field = EditField::NONE;
  };

  /// 정보 수정 세션 관리 (ChatID -> 수정 세션)
  QMap<QString, EditSession> m_editSessions;

  /// 사용자 정보 리포지토리 (영속화)
  UserRepository m_userRepository;
  ParkingRepository m_parkingRepository;
  ocr::recognition::TelegramLlmRunner *m_llmRunner = nullptr;
};

#endif // TELEGRAMBOTAPI_H
