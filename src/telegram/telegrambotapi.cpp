#include "telegrambotapi.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QUrlQuery>

/**
 * @brief 생성자
 */
TelegramBotAPI::TelegramBotAPI(QObject *parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)),
      m_pollTimer(new QTimer(this)) {
  // 환경변수에서 봇 토큰 읽기
  m_botToken =
      QProcessEnvironment::systemEnvironment().value("TELEGRAM_BOT_TOKEN");

  if (m_botToken.isEmpty()) {
    qWarning() << "[Telegram] TELEGRAM_BOT_TOKEN 환경변수가 설정되지 "
                  "않았습니다.";
  }

  // 시그널 연결 후 로그가 출력되도록 지연 실행
  QTimer::singleShot(0, this, [this]() {
    if (m_botToken.isEmpty()) {
      emit logMessage("[Telegram] ⚠️ TELEGRAM_BOT_TOKEN 환경변수가 "
                      "설정되지 않았습니다. 환경변수를 설정해주세요.");
    } else {
      emit logMessage(QString("[Telegram] ✅ 봇 토큰 로드 완료 (길이: %1)")
                          .arg(m_botToken.length()));
    }

    // DB에서 사용자 로드
    QString dbErr;
    if (m_userRepository.init(&dbErr)) {
      m_chatToPlate = m_userRepository.getAllUsers();
      // 역방향 맵 구성
      m_plateToChat.clear();
      for (auto it = m_chatToPlate.begin(); it != m_chatToPlate.end(); ++it) {
        m_plateToChat.insert(it.value(), it.key());
      }
      emit logMessage(QString("[Telegram] 📂 저장된 사용자 %1명 로드 완료")
                          .arg(m_chatToPlate.size()));
      emit usersUpdated(m_chatToPlate.size());
    } else {
      emit logMessage(
          QString("[Telegram] ⚠️ 사용자 DB 초기화 실패: %1").arg(dbErr));
    }

    // 시그널 연결 후 폴링 시작
    startPolling();
  });
}

/* ============================================================
 * 입차 알림 (차량번호 매칭 사용자에게만 전송)
 * ============================================================ */

void TelegramBotAPI::sendEntryNotice(const QString &plateNumber) {
  QString targetChatId = m_plateToChat.value(plateNumber);

  if (targetChatId.isEmpty()) {
    emit logMessage(
        QString("[Telegram] ⚠️ 차량번호 '%1'에 등록된 사용자가 없습니다.")
            .arg(plateNumber));
    return;
  }

  emit logMessage(
      QString("[Telegram] 입차 알림 발송 중... 차량: %1 → 사용자에게")
          .arg(plateNumber));

  QString text = QString("🅿️ *입차 안내*\n\n"
                         "차량번호: `%1`\n"
                         "입차 처리가 완료되었습니다.")
                     .arg(plateNumber);

  sendMessage(targetChatId, text);
}

/* ============================================================
 * 출차 알림 (차량번호 매칭 사용자에게만 전송)
 * ============================================================ */

void TelegramBotAPI::sendExitNotice(const QString &plateNumber, int fee) {
  QString targetChatId = m_plateToChat.value(plateNumber);

  if (targetChatId.isEmpty()) {
    emit logMessage(
        QString("[Telegram] ⚠️ 차량번호 '%1'에 등록된 사용자가 없습니다.")
            .arg(plateNumber));
    return;
  }

  emit logMessage(
      QString(
          "[Telegram] 출차 알림 발송 중... 차량: %1, 요금: %2원 → 사용자에게")
          .arg(plateNumber)
          .arg(fee));

  QString text = QString("🚗 *출차 안내*\n\n"
                         "차량번호: `%1`\n"
                         "주차 요금: *%2원*")
                     .arg(plateNumber)
                     .arg(fee);

  // 모의 결제 버튼 (Callback 방식)
  QString replyMarkup;
  // URL이 있든 없든, 요금이 있으면 결제 버튼 표시 (모의 결제용)
  if (fee > 0) {
    QJsonObject btn;
    btn["text"] = QString::fromUtf8("💰 결제하기");
    // callback_data: PAY_차량번호_금액
    btn["callback_data"] = QString("PAY_%1_%2").arg(plateNumber).arg(fee);

    QJsonArray row;
    row.append(btn);

    QJsonArray keyboard;
    keyboard.append(row);

    QJsonObject markup;
    markup["inline_keyboard"] = keyboard;

    replyMarkup =
        QString::fromUtf8(QJsonDocument(markup).toJson(QJsonDocument::Compact));
  }

  sendMessage(targetChatId, text, replyMarkup);
}

/* ============================================================
 * 메인 메뉴 전송
 * ============================================================ */

void TelegramBotAPI::sendMainMenu(const QString &chatId) {
  QJsonArray row1, row2;

  // Row 1
  QJsonObject btn1, btn2;
  btn1["text"] = QString::fromUtf8("🅿️ 내 주차 현황");
  btn2["text"] = QString::fromUtf8("👤 내 정보");
  row1.append(btn1);
  row1.append(btn2);

  // Row 2
  QJsonObject btn3, btn4;
  btn3["text"] = QString::fromUtf8("💳 최근 이용 내역");
  btn4["text"] = QString::fromUtf8("📞 관리자 호출");
  row2.append(btn3);
  row2.append(btn4);

  QJsonArray keyboard;
  keyboard.append(row1);
  keyboard.append(row2);

  QJsonObject markup;
  markup["keyboard"] = keyboard;
  markup["resize_keyboard"] = true;    // 버튼 크기 자동 조절
  markup["one_time_keyboard"] = false; // 메뉴 계속 유지

  QString replyMarkup =
      QString::fromUtf8(QJsonDocument(markup).toJson(QJsonDocument::Compact));

  sendMessage(chatId, "원하시는 메뉴를 선택해주세요.", replyMarkup);
}

/* ============================================================
 * 유틸리티
 * ============================================================ */

int TelegramBotAPI::registeredUserCount() const { return m_plateToChat.size(); }

/* ============================================================
 * Telegram sendMessage API
 * ============================================================ */

void TelegramBotAPI::sendMessage(const QString &chatId, const QString &text,
                                 const QString &replyMarkup) {
  if (m_botToken.isEmpty()) {
    emit logMessage("[Telegram] Error: 봇 토큰이 설정되지 않았습니다.");
    emit messageSent(false, "No bot token");
    return;
  }

  QUrl url(
      QString("https://api.telegram.org/bot%1/sendMessage").arg(m_botToken));

  QJsonObject body;
  body["chat_id"] = chatId;
  body["text"] = text;
  body["parse_mode"] = "Markdown";

  if (!replyMarkup.isEmpty()) {
    body["reply_markup"] =
        QJsonDocument::fromJson(replyMarkup.toUtf8()).object();
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QByteArray postData = QJsonDocument(body).toJson(QJsonDocument::Compact);

  QNetworkReply *reply = m_networkManager->post(request, postData);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply]() { handleReply(reply); });
}

void TelegramBotAPI::answerCallbackQuery(const QString &callbackQueryId,
                                         const QString &text) {
  if (m_botToken.isEmpty())
    return;

  QUrl url(QString("https://api.telegram.org/bot%1/answerCallbackQuery")
               .arg(m_botToken));
  QJsonObject body;
  body["callback_query_id"] = callbackQueryId;
  if (!text.isEmpty()) {
    body["text"] = text;
  }

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  QByteArray postData = QJsonDocument(body).toJson(QJsonDocument::Compact);

  QNetworkReply *reply = m_networkManager->post(request, postData);
  connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

/* ============================================================
 * API 응답 처리
 * ============================================================ */

void TelegramBotAPI::handleReply(QNetworkReply *reply) {
  QByteArray responseData = reply->readAll();
  int statusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  if (reply->error() == QNetworkReply::NoError) {
    emit logMessage("[Telegram] ✅ 메시지 전송 성공!");
    emit messageSent(true, QString::fromUtf8(responseData));
  } else {
    emit logMessage(QString("[Telegram] ❌ 전송 실패 (HTTP %1): %2")
                        .arg(statusCode)
                        .arg(QString::fromUtf8(responseData)));
    emit messageSent(false, QString::fromUtf8(responseData));
  }

  reply->deleteLater();
}

/* ============================================================
 * getUpdates Long Polling
 * ============================================================ */

void TelegramBotAPI::startPolling() {
  // 롱 폴링을 위해 타이머는 연결 끊김 시 재시도용으로만 사용
  connect(m_pollTimer, &QTimer::timeout, this, &TelegramBotAPI::pollUpdates);

  // 첫 폴링 시작
  pollUpdates();
}

void TelegramBotAPI::pollUpdates() {
  if (m_botToken.isEmpty()) {
    return;
  }

  // 폴링 중에는 타이머 중지
  m_pollTimer->stop();

  QUrl url(
      QString("https://api.telegram.org/bot%1/getUpdates").arg(m_botToken));

  QUrlQuery query;
  // timeout: 롱 폴링 대기 시간 (초 단위). 네트워크 환경 안정을 위해 20초로
  // 단축.
  query.addQueryItem("timeout", "20");
  // offset: 확인하지 않은 새 메시지부터 가져오기
  if (m_lastUpdateId > 0) {
    query.addQueryItem("offset", QString::number(m_lastUpdateId + 1));
  }
  url.setQuery(query);

  QNetworkRequest request(url);
  // 네트워크 타임아웃을 롱 폴링 시간보다 약간 넉넉하게 설정 (25초)
  request.setTransferTimeout(25000);

  QNetworkReply *reply = m_networkManager->get(request);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    QByteArray data = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
      QNetworkReply::NetworkError err = reply->error();
      if (err != QNetworkReply::OperationCanceledError &&
          err != QNetworkReply::TimeoutError &&
          err != QNetworkReply::RemoteHostClosedError) {
        emit logMessage(QString("[Telegram] Polling Error (Code: %1): %2")
                            .arg(static_cast<int>(err))
                            .arg(reply->errorString()));
        m_pollTimer->start(5000); // 5초 후 재시도
      } else {
        // 네트워크 끊김이나 타임아웃은 일반적인 상황이므로 조용히 즉시 재시도
        pollUpdates();
      }
      reply->deleteLater();
      return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    if (root.contains("error_code")) {
      int errorCode = root["error_code"].toInt();
      // Conflict Error (409) handling
      if (errorCode == 409 || errorCode == 206) {
        emit logMessage("[Telegram] ⚠️ 봇 중복 실행 감지! 다른 곳에서 봇이 실행 "
                        "중이거나 웹훅이 설정되어 있습니다.");
        m_pollTimer->start(10000); // 10초 후 재시도 (빈도 줄임)
        reply->deleteLater();
        return;
      }
    }

    if (!root["ok"].toBool()) {
      int errorCode = root["error_code"].toInt();
      QString description = root["description"].toString();
      emit logMessage(QString("[Telegram] getUpdates failed: %1 (Error: %2)")
                          .arg(description)
                          .arg(errorCode));
      m_pollTimer->start(5000);
      reply->deleteLater();
      return;
    }

    QJsonArray results = root["result"].toArray();
    int newUsers = 0;

    for (const QJsonValue &val : results) {
      QJsonObject update = val.toObject();
      qint64 updateId = update["update_id"].toVariant().toLongLong();

      if (updateId > m_lastUpdateId) {
        m_lastUpdateId = updateId;
      }

      // =====================================================
      // 1. 콜백 쿼리 처리 (버튼 클릭)
      // =====================================================
      if (update.contains("callback_query")) {
        QJsonObject callback = update["callback_query"].toObject();
        QString id = callback["id"].toString();
        QString data = callback["data"].toString();
        QJsonObject from = callback["from"].toObject();
        QString firstName = from["first_name"].toString();
        QJsonObject message = callback["message"].toObject();
        QString chatId = QString::number(
            message["chat"].toObject()["id"].toVariant().toLongLong());

        // 결제 요청 처리
        if (data.startsWith("PAY_")) {
          QStringList parts = data.split('_');
          // PAY_plate_amount
          if (parts.size() >= 3) {
            QString plate = parts[1];
            int amount = parts[2].toInt();

            emit logMessage(
                QString("[Telegram] 💰 결제 확인됨: %1 (%2원) by %3")
                    .arg(plate)
                    .arg(amount)
                    .arg(firstName));
            emit paymentConfirmed(plate, amount);

            // 1) 팝업 알림 (Toast)
            answerCallbackQuery(id, "결제가 완료되었습니다!");

            // 2) 메시지 전송
            sendMessage(chatId,
                        QString("✅ 결제가 성공적으로 완료되었습니다.\n(차량: "
                                "%1, 금액: %2원)")
                            .arg(plate)
                            .arg(amount));
          }
        }
        continue; // 콜백 처리는 여기서 끝
      }

      // =====================================================
      // 2. 일반 메시지 처리
      // =====================================================
      if (!update.contains("message"))
        continue;

      QJsonObject message = update["message"].toObject();
      QString text = message["text"].toString().trimmed();
      QString chatId = QString::number(
          message["chat"].toObject()["id"].toVariant().toLongLong());
      QString firstName = message["chat"].toObject()["first_name"].toString();

      if (chatId.isEmpty() || chatId == "0") {
        emit logMessage("[Telegram] ⚠️ Invalid Chat ID parsed. Skipping.");
        continue;
      }

      emit logMessage(
          QString("[Telegram] 📩 Msg received - From: %1 (%2), Text: %3")
              .arg(firstName, chatId, text));

      // 1. /start 메시지 처리
      if (text == "/start") {
        if (m_plateToChat.values().contains(chatId)) {
          sendMainMenu(chatId); // 이미 등록됨 -> 메인 메뉴 표시
        } else {
          // 키보드 버튼 생성
          QJsonObject btnReg;
          btnReg["text"] = QString::fromUtf8("📝 차량 등록하기");

          QJsonArray row;
          row.append(btnReg);

          QJsonArray keyboard;
          keyboard.append(row);

          QJsonObject markup;
          markup["keyboard"] = keyboard;
          markup["resize_keyboard"] = true;
          markup["one_time_keyboard"] = true;

          QString replyMarkup = QString::fromUtf8(
              QJsonDocument(markup).toJson(QJsonDocument::Compact));

          sendMessage(chatId,
                      QString("안녕하세요, %1님! 👋\n"
                              "아래 버튼을 눌러 차량을 등록해주세요.")
                          .arg(firstName),
                      replyMarkup);

          emit logMessage(
              QString("[Telegram] 👋 입장: %1 (등록 대기)").arg(firstName));
        }
      }
      // 2. '차량 등록하기' 버튼 클릭 처리
      else if (text == QString::fromUtf8("📝 차량 등록하기")) {
        m_pendingRegistration.insert(chatId);
        // 키보드 제거하면서 메시지 전송
        sendMessage(chatId, "차량번호를 입력해주세요.\n(예: 123가4567)",
                    QString("{\"remove_keyboard\": true}"));
        emit logMessage(QString("[Telegram] 📝 등록 요청: %1 (차량번호 입력 "
                                "대기) [Pending Count: %2]")
                            .arg(firstName)
                            .arg(m_pendingRegistration.size()));
      }
      // 3. 차량번호 입력 처리 (대기 목록에 있는 경우)
      else if (m_pendingRegistration.contains(chatId)) {
        emit logMessage(QString("[Telegram] 🔍 %1님의 차량번호 입력 감지: %2")
                            .arg(firstName, text));
        if (text.length() >= 7) {
          // DB 영속화
          QString pushErr;
          // 이름은 firstName 사용, 전화번호는 수집하지 않음 ("")
          if (m_userRepository.registerUser(chatId, text, firstName, "",
                                            &pushErr)) {
            m_plateToChat.insert(text, chatId);
            m_chatToPlate.insert(chatId, text); // 역방향 매핑 추가
            m_pendingRegistration.remove(chatId);
            ++newUsers;

            emit logMessage(
                QString("[Telegram] ✅ 사용자 등록 완료 및 저장: %1 (차량: %2)")
                    .arg(firstName)
                    .arg(text));

            sendMessage(
                chatId,
                QString("등록 완료되었습니다! 🎉\n"
                        "이제 차량 **%1**의 입출차 알림을 받으실 수 있습니다.")
                    .arg(text));

            // 등록 완료 후 메인 메뉴 표시
            sendMainMenu(chatId);
          } else {
            emit logMessage(
                QString("[Telegram] ❌ 사용자 저장 실패: %1").arg(pushErr));
            sendMessage(chatId, "죄송합니다. 내부 오류로 등록에 실패했습니다. "
                                "관리자에게 문의해주세요.");
          }
        } else {
          sendMessage(chatId, "올바른 차량번호 형식이 아닌 것 같아요. 다시 "
                              "입력해주세요. (예: 123가4567)");
        }
      }
      // 4. 메인 메뉴 버튼 처리
      else if (text == QString::fromUtf8("🅿️ 내 주차 현황")) {
        QString plate = m_chatToPlate.value(chatId);
        if (plate.isEmpty()) {
          sendMessage(
              chatId,
              "차량 등록 정보가 없습니다. /start 를 눌러 등록해주세요.");
        } else {
          sendMessage(chatId, QString("🅿️ *주차 현황*\n\n"
                                      "차량번호: `%1`\n"
                                      "상태: 입차 중\n"
                                      "입차시간: 2024-02-11 10:00\n"
                                      "현재 요금: *5,000원*")
                                  .arg(plate));
        }
      } else if (text == QString::fromUtf8("👤 내 정보")) {
        QString plate = m_chatToPlate.value(chatId);
        if (plate.isEmpty()) {
          sendMessage(chatId, "등록된 차량이 없습니다.");
        } else {
          sendMessage(chatId, QString("👤 *내 정보*\n\n"
                                      "등록된 차량번호: `%1`\n"
                                      "상태: 정상 등록됨")
                                  .arg(plate));
        }
      } else if (text == QString::fromUtf8("💳 최근 이용 내역")) {
        sendMessage(chatId, "최근 3개월 간 이용 내역이 없습니다.");
      } else if (text == QString::fromUtf8("📞 관리자 호출")) {
        sendMessage(
            chatId,
            "📞 관리자에게 호출 메시지를 보냈습니다.\n잠시만 기다려주세요.");
        emit logMessage(QString("[Telegram] 🚨 관리자 호출 요청! (User: %1)")
                            .arg(firstName));
        emit adminSummoned(chatId, firstName);
      }
    }

    if (newUsers > 0) {
      emit usersUpdated(m_plateToChat.size());
    }

    reply->deleteLater();

    // 처리가 끝나면 즉시 다음 폴링 시작 (재귀 호출 효과)
    pollUpdates();
  });
}
