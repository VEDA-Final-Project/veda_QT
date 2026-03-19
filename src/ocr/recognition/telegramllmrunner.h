#ifndef TELEGRAMLLMRUNNER_H
#define TELEGRAMLLMRUNNER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace ocr::recognition {

/**
 * @brief 사용자의 텔레그램 메시지를 분석하여 명령 카테고리로 분류하는 클래스
 */
class TelegramLlmRunner : public QObject {
  Q_OBJECT

public:
  enum class CommandCategory {
    INFO_INQUIRY,   // 내 정보 조회
    FEE_INQUIRY,    // 요금 조회
    USAGE_HISTORY,  // 이용 내역
    CALL_ADMIN,     // 관리자 호출
    FEE_PAYMENT,    // 요금 납부
    NONE            // 해당 없음
  };

  explicit TelegramLlmRunner(QObject *parent = nullptr);
  ~TelegramLlmRunner() override;

  /**
   * @brief 사용자 텍스트를 분석하여 카테고리를 반환합니다 (동기 방식 시뮬레이션)
   */
  CommandCategory classifyCommand(const QString &userText);

private:
  QString buildJsonRequest(const QString &userText) const;
  CommandCategory parseResponse(const QByteArray &response) const;
};

} // namespace ocr::recognition

#endif // TELEGRAMLLMRUNNER_H
