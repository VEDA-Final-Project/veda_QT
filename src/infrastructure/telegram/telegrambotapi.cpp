#include "telegrambotapi.h"
#include "domain/parking/parkingfeepolicy.h"
#include <QJsonDocument>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QUrlQuery>
#include <QDateTime>

/**
 * @brief 생성자
 */
TelegramBotAPI::TelegramBotAPI(QObject *parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)),
      m_pollTimer(new QTimer(this)), m_llmRunner(new ocr::recognition::TelegramLlmRunner(this)) {
  // 환경변수에서 봇 토큰 읽기
  m_botToken = QProcessEnvironment::systemEnvironment().value("TELEGRAM_BOT_TOKEN");
  
  // Repository 초기화
  m_parkingRepository.init();

  if (m_botToken.isEmpty()) {
    qWarning() << "[Telegram] TELEGRAM_BOT_TOKEN 환경변수가 설정되지 않았습니다.";
  }

  // 시그널 연결 후 로그가 출력되도록 지연 실행
  QTimer::singleShot(0, this, [this]() {
    if (m_botToken.isEmpty()) {
      emit logMessage("[Telegram] ⚠️ TELEGRAM_BOT_TOKEN 환경변수가 설정되지 않았습니다. 환경변수를 설정해주세요.");
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

void TelegramBotAPI::sendExitNotice(const QString &plateNumber, int fee,
                                    int paymentRecordId) {
  QString targetChatId = m_plateToChat.value(plateNumber);

  if (targetChatId.isEmpty()) {
    emit logMessage(
        QString("[Telegram] ⚠️ 차량번호 '%1'에 등록된 사용자가 없습니다.")
            .arg(plateNumber));
    return;
  }

  const int resolvedFee = fee < 0 ? 0 : fee;

  emit logMessage(
      QString(
          "[Telegram] 출차 알림 발송 중... 차량: %1, 요금: %2원 → 사용자에게")
          .arg(plateNumber)
          .arg(resolvedFee));

  QString text;
  if (resolvedFee <= 0) {
    text = QString("🚗 *출차 안내*\n\n"
                   "차량번호: `%1`\n"
                   "5분 이내에 출차시 무료입니다.")
               .arg(plateNumber);
  } else {
    text = QString("🚗 *출차 안내*\n\n"
                   "차량번호: `%1`\n"
                   "주차 요금: *%2원*")
               .arg(plateNumber)
               .arg(resolvedFee);
  }

  QString replyMarkup;
  if (resolvedFee > 0 && paymentRecordId > 0) {
    QJsonObject btn;
    btn["text"] = QString::fromUtf8("💰 결제하기");
    btn["callback_data"] = QString("PAYREC_%1").arg(paymentRecordId);

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

void TelegramBotAPI::sendMainMenu(const QString &chatId,
                                  const QString &customMessage) {
  QJsonArray row1, row2;

  QJsonObject btn1, btn2;
  btn1["text"] = QString::fromUtf8("👤 내 정보 조회");
  btn2["text"] = QString::fromUtf8("💳 요금 조회");
  row1.append(btn1);
  row1.append(btn2);

  QJsonObject btn3, btn4;
  btn3["text"] = QString::fromUtf8("🕒 최근 이용 내역");
  btn4["text"] = QString::fromUtf8("🚨 관리자 호출");
  row2.append(btn3);
  row2.append(btn4);

  QJsonArray keyboard;
  keyboard.append(row1);
  keyboard.append(row2);

  QJsonObject markup;
  markup["keyboard"] = keyboard;
  markup["resize_keyboard"] = true;
  markup["one_time_keyboard"] = false;

  QString replyMarkup =
      QString::fromUtf8(QJsonDocument(markup).toJson(QJsonDocument::Compact));

  QString text = customMessage.isEmpty() ? QString::fromUtf8("🏠 *메인 메뉴*")
                                         : customMessage;
  sendMessage(chatId, text, replyMarkup);
}

void TelegramBotAPI::removeUser(const QString &chatId) {
  if (m_chatToPlate.contains(chatId)) {
    QString plate = m_chatToPlate.value(chatId);
    m_chatToPlate.remove(chatId);
    // 한 차량에 여러 ChatID가 매핑될 수 있는 구조라면 value로 키 찾기
    QString keyToRemove;
    for (auto it = m_plateToChat.begin(); it != m_plateToChat.end(); ++it) {
      if (it.value() == chatId) {
        keyToRemove = it.key();
        break;
      }
    }
    if (!keyToRemove.isEmpty()) {
      m_plateToChat.remove(keyToRemove);
    }
    emit logMessage(QString("[Telegram] 🗑️ 메모리에서 사용자 동기화 삭제 완료 "
                            "(ChatID: %1, 차량: %2)")
                        .arg(chatId, plate));
    emit usersUpdated(m_chatToPlate.size());
  }
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
    emit logMessage("[Telegram] Message sent successfully!");
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
        if (data.startsWith("PAYREC_")) {
          const int recordId = data.mid(QStringLiteral("PAYREC_").size()).toInt();
          const QJsonObject record = m_parkingRepository.findLogById(recordId);
          if (record.isEmpty()) {
            answerCallbackQuery(id, "결제 대상 내역을 찾을 수 없습니다.");
            sendMessage(chatId, "결제 대상 내역을 찾을 수 없습니다. 최신 요금 조회 후 다시 시도해주세요.");
            continue;
          }

          const QString exitTime = record["exit_time"].toString().trimmed();
          const QString payStatus = record["pay_status"].toString().trimmed();
          const QString plate = record["plate_number"].toString().trimmed();
          const int amount = record["total_amount"].toInt();

          if (exitTime.isEmpty()) {
            answerCallbackQuery(id, "출차 완료 후 결제할 수 있습니다.");
            sendMessage(chatId, "이 내역은 아직 출차 처리되지 않았습니다.");
            continue;
          }
          if (payStatus != QStringLiteral("정산대기")) {
            answerCallbackQuery(id, "이미 처리된 결제입니다.");
            sendMessage(chatId, "이미 결제가 완료된 내역입니다.");
            continue;
          }

          QString error;
          if (!m_parkingRepository.markPaymentById(recordId, amount,
                                                   QStringLiteral("결제완료"),
                                                   &error)) {
            answerCallbackQuery(id, "결제 반영에 실패했습니다.");
            sendMessage(chatId,
                        error.isEmpty()
                            ? QStringLiteral("결제 반영에 실패했습니다. 다시 시도해주세요.")
                            : QStringLiteral("결제 반영에 실패했습니다: %1").arg(error));
            continue;
          }

          emit logMessage(
              QString("[Telegram] 💰 결제 확인됨: recordId=%1, %2 (%3원) by %4")
                  .arg(recordId)
                  .arg(plate)
                  .arg(amount)
                  .arg(firstName));
          emit paymentConfirmed(recordId, plate, amount);

          answerCallbackQuery(id, "결제가 완료되었습니다!");
          sendMessage(chatId,
                      QString("✅ 결제가 성공적으로 완료되었습니다.\n(차량: %1, 금액: %2원)")
                          .arg(plate)
                          .arg(amount));
        } else if (data.startsWith("PAY_")) {
          answerCallbackQuery(id, "기존 결제 링크는 만료되었습니다.");
          sendMessage(chatId,
                      "기존 결제 링크는 만료되었습니다. 최신 요금 조회 후 다시 결제해주세요.");
        }
        // 수정하기 → 필드 선택 화면 표시
        else if (data == "edit_select") {
          answerCallbackQuery(id, "");
          m_editSessions[chatId] = EditSession{};

          // 인라인 버튼: 수정할 항목 선택
          auto makeBtn = [](const QString &text, const QString &cb) {
            QJsonObject btn;
            btn["text"] = text;
            btn["callback_data"] = cb;
            return btn;
          };
          QJsonArray row1, row2;
          row1.append(makeBtn("✏️ 이름", "edit_name"));
          row1.append(makeBtn("🚗 차량번호", "edit_plate"));
          row2.append(makeBtn("📱 전화번호", "edit_phone"));
          row2.append(makeBtn("💳 카드번호", "edit_card"));

          QJsonArray keyboard;
          keyboard.append(row1);
          keyboard.append(row2);

          QJsonObject markup;
          markup["inline_keyboard"] = keyboard;
          QString replyMarkup = QString::fromUtf8(
              QJsonDocument(markup).toJson(QJsonDocument::Compact));

          sendMessage(
              chatId,
              QString::fromUtf8("✏️ *정보 수정*\n\n수정할 항목을 선택해주세요:"),
              replyMarkup);
        }
        // 개별 필드 선택
        else if (data == "edit_name" || data == "edit_plate" ||
                 data == "edit_phone" || data == "edit_card") {
          answerCallbackQuery(id, "");
          EditSession session;
          QString promptMsg;
          if (data == "edit_name") {
            session.field = EditField::NAME;
            promptMsg = QString::fromUtf8("✏️ 새 이름을 입력해주세요:");
          } else if (data == "edit_plate") {
            session.field = EditField::PLATE;
            promptMsg = QString::fromUtf8(
                "🚗 새 차량번호를 입력해주세요:\n(예: 123가4567)");
          } else if (data == "edit_phone") {
            session.field = EditField::PHONE;
            promptMsg = QString::fromUtf8(
                "📱 새 전화번호를 입력해주세요:\n(예: 010-1234-5678)");
          } else {
            session.field = EditField::CARD;
            promptMsg =
                QString::fromUtf8("💳 새 카드번호 16자리를 입력해주세요:");
          }
          m_editSessions[chatId] = session;

          // 취소(메뉴 복귀) 인라인 버튼
          QJsonObject btnCancel;
          btnCancel["text"] = QString::fromUtf8("❌ 취소");
          btnCancel["callback_data"] = "edit_cancel";
          QJsonArray row;
          row.append(btnCancel);
          QJsonArray keyboard;
          keyboard.append(row);
          QJsonObject markup;
          markup["inline_keyboard"] = keyboard;

          sendMessage(
              chatId,
              QString::fromUtf8("✏️ 수정 중입니다.\n\n") + promptMsg +
                  QString::fromUtf8("\n\n(취소하려면 ❌ 취소 버튼을 누르세요)"),
              QString::fromUtf8(
                  QJsonDocument(markup).toJson(QJsonDocument::Compact)));
        }
        // 수정 취소 → 메뉴로 바로
        else if (data == "edit_cancel") {
          answerCallbackQuery(id, "");
          m_editSessions.remove(chatId);
          sendMainMenu(chatId);
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

      // 1. /start 메시지 처리 (환영 인사 + 등록 안내 결합)
      if (text == "/start") {
        m_registrationSessions.remove(chatId); // 기존 등록 세션 초기화
        m_editSessions.remove(chatId);         // 기존 수정 세션 초기화
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

          sendMessage(
              chatId,
              "🚗 *안녕하세요! 스마트 주차 관리 봇입니다.*\n\n"
              "텔레그램으로 언제 어디서나 간편하게 주차 서비스를 "
              "이용해보세요.\n\n"
              "📝 *원활한 서비스 이용을 위해 사용자 등록이 필요합니다.*\n"
              "아래 버튼을 누른 후 차량번호를 입력해주세요.",
              replyMarkup);

          emit logMessage(QString("[Telegram] 👋 입장: %1 (환영 및 등록 안내)")
                              .arg(firstName));
        }
      }
      // 1-b. '메뉴로 돌아가기' 텍스트 (어느 상태에서나 세션 클리어 후 메뉴)
      else if (text.contains(QString::fromUtf8("메뉴로 돌아가기"))) {
        m_editSessions.remove(chatId);
        m_registrationSessions.remove(chatId);
        sendMainMenu(chatId);
      }
      // 2. '차량 등록하기' 버튼 클릭 처리
      else if (text.contains(QString::fromUtf8("차량 등록하기"))) {
        RegistrationData &data = m_registrationSessions[chatId];
        data.state = RegistrationState::WAIT_PLATE;
        data.name = firstName;

        sendMessage(chatId, "차량번호를 입력해주세요.\n(예: 123가4567)",
                    "{\"remove_keyboard\": true}");
        emit logMessage(
            QString("[Telegram] 📝 등록 요청: %1 (차량번호 입력 대기 시작)")
                .arg(firstName));
      }
      // 3. 수정 세션 처리 (수정할 값 입력 대기 중)
      else if (m_editSessions.contains(chatId) &&
               m_editSessions[chatId].field != EditField::NONE) {
        EditSession &editSess = m_editSessions[chatId];

        // 기존 사용자 정보 조회
        UserRepository editRepo;
        const QVector<QJsonObject> allForEdit = editRepo.getAllUsersFull();
        QJsonObject existingUser;
        for (const QJsonObject &u : allForEdit) {
          if (u["chat_id"].toString() == chatId) {
            existingUser = u;
            break;
          }
        }

        if (existingUser.isEmpty()) {
          sendMessage(chatId,
                      QString::fromUtf8("❌ 사용자 정보를 찾을 수 없습니다."));
          m_editSessions.remove(chatId);
          sendMainMenu(chatId);
        } else {
          QString newName = existingUser["name"].toString();
          QString newPlate = existingUser["plate_number"].toString();
          QString newPhone = existingUser["phone"].toString();
          QString newCard = existingUser["payment_info"].toString();

          bool valid = true;
          QString fieldLabel;

          if (editSess.field == EditField::NAME) {
            newName = text;
            fieldLabel = "이름";
          } else if (editSess.field == EditField::PLATE) {
            if (text.length() < 7) {
              sendMessage(chatId, QString::fromUtf8(
                                      "⚠️ 올바른 차량번호 형식이 아닙니다.\n"
                                      "다시 입력해주세요."));
              valid = false;
            } else {
              newPlate = text;
              fieldLabel = "차량번호";
            }
          } else if (editSess.field == EditField::PHONE) {
            newPhone = text;
            fieldLabel = "전화번호";
          } else if (editSess.field == EditField::CARD) {
            QString digits = text;
            digits.remove('-').remove(' ');
            if (digits.length() != 16) {
              sendMessage(chatId, QString::fromUtf8(
                                      "⚠️ 카드번호는 16자리 숫자여야 합니다.\n"
                                      "다시 입력해주세요."));
              valid = false;
            } else {
              newCard = digits;
              fieldLabel = "카드번호";
            }
          }

          if (valid) {
            if (editRepo.updateUser(chatId, newPlate, newName, newPhone,
                                    newCard)) {
              // 캐시도 갱신
              m_chatToPlate[chatId] = newPlate;
              m_plateToChat.remove(existingUser["plate_number"].toString());
              m_plateToChat[newPlate] = chatId;
              m_editSessions.remove(chatId);
              emit usersUpdated(m_chatToPlate.size());
              sendMainMenu(
                  chatId,
                  QString::fromUtf8("✅ *%1*이(가) 성공적으로 수정되었습니다!")
                      .arg(fieldLabel));
            } else {
              m_editSessions.remove(chatId);
              sendMainMenu(
                  chatId, QString::fromUtf8("❌ 수정 중 오류가 발생했습니다."));
            }
          }
        }
      }
      // 4. 등록 단계별 처리
      else if (m_registrationSessions.contains(chatId)) {
        RegistrationData &data = m_registrationSessions[chatId];

        if (data.state == RegistrationState::WAIT_PLATE) {
          if (text.length() >= 7) {
            data.plate = text;
            data.state = RegistrationState::WAIT_PHONE;

            // 연락처 공유 버튼 생성
            QJsonObject btnPhone;
            btnPhone["text"] = QString::fromUtf8("📱 연락처 공유하기");
            btnPhone["request_contact"] = true;

            QJsonArray row;
            row.append(btnPhone);
            QJsonArray keyboard;
            keyboard.append(row);
            QJsonObject markup;
            markup["keyboard"] = keyboard;
            markup["resize_keyboard"] = true;
            markup["one_time_keyboard"] = true;

            QString replyMarkup = QString::fromUtf8(
                QJsonDocument(markup).toJson(QJsonDocument::Compact));

            sendMessage(chatId,
                        "연락처를 공유해주세요.\n(아래 버튼을 눌러주세요)",
                        replyMarkup);
            emit logMessage(
                QString("[Telegram] 🖋️ 차량번호 입력: %1 -> %2 (연락처 대기)")
                    .arg(firstName, text));
          } else {
            sendMessage(
                chatId,
                "⚠️ 올바른 차량번호 형식이 아닌 것 같아요. 다시 입력해주세요.");
          }
        } else if (data.state == RegistrationState::WAIT_PHONE) {
          QString phone;
          if (message.contains("contact")) {
            phone = message["contact"].toObject()["phone_number"].toString();
          } else {
            // 텍스트 입력: 숫자/+/-만 포함된 경우에만 전화번호로 인정
            QString stripped = text;
            stripped.remove('-').remove(' ');
            bool looksLikePhone =
                stripped.length() >= 9 && stripped.length() <= 15 &&
                (stripped.startsWith('+') || stripped.startsWith('0')) &&
                stripped.mid(stripped.startsWith('+') ? 1 : 0).toLongLong() > 0;
            if (looksLikePhone) {
              phone = text;
            }
          }

          if (!phone.isEmpty()) {
            data.phone = phone;
            data.state = RegistrationState::WAIT_CARD;
            sendMessage(chatId,
                        "결제하실 카드번호를 입력해주세요.\n(16자리 숫자)",
                        "{\"remove_keyboard\": true}");
            emit logMessage(
                QString("[Telegram] 📱 연락처 입력: %1 -> %2 (카드번호 대기)")
                    .arg(firstName, phone));
          } else {
            sendMessage(chatId, "⚠️ 올바른 전화번호 형식이 아닙니다.\n"
                                "아래 '연락처 공유하기' 버튼을 누르거나,\n"
                                "전화번호를 직접 입력해주세요.\n"
                                "(예: 010-1234-5678)");
          }
        } else if (data.state == RegistrationState::WAIT_CARD) {
          // 카드번호 유효성 검사: 숫자만, 16자리
          QString cardDigits = text;
          cardDigits.remove('-').remove(' ');
          bool validCard = cardDigits.length() == 16;
          if (validCard) {
            for (int ci = 0; ci < cardDigits.length(); ++ci) {
              if (!cardDigits.at(ci).isDigit()) {
                validCard = false;
                break;
              }
            }
          }

          if (!validCard) {
            sendMessage(chatId, "⚠️ 카드번호 형식이 올바르지 않습니다.\n"
                                "16자리 숫자를 입력해주세요.\n"
                                "(예: 1234567890123456)");
          } else {
            QString pushErr;
            if (m_userRepository.registerUser(chatId, data.plate, data.name,
                                              data.phone, cardDigits,
                                              &pushErr)) {
              m_plateToChat.insert(data.plate, chatId);
              m_chatToPlate.insert(chatId, data.plate);
              ++newUsers;

              emit logMessage(
                  QString("[Telegram] ✅ 가입 완료: %1 (차량: %2, 연락처: %3)")
                      .arg(data.name, data.plate, data.phone));

              sendMainMenu(chatId, "✅ *등록이 완료되었습니다!*\n\n"
                                   "메인 메뉴를 이용해주세요.");

              // data 참조 오류를 방지하기 위해 로깅 후 세션 삭제
              m_registrationSessions.remove(chatId);
            } else {
              sendMessage(chatId,
                          "❌ 등록 중 오류가 발생했습니다. 다시 시도해주세요.");
              m_registrationSessions.remove(chatId);
            }
          }
        }
      }
      // 5. 메인 메뉴 버튼 처리
      else if (text.contains(QString::fromUtf8("내 정보 조회"))) {
        // 메모리 캐시 대신 DB에서 직접 최신 정보 조회
        UserRepository userRepo;
        const QVector<QJsonObject> allUsers = userRepo.getAllUsersFull();
        QJsonObject userInfo;
        for (const QJsonObject &u : allUsers) {
          if (u["chat_id"].toString() == chatId) {
            userInfo = u;
            break;
          }
        }

        if (userInfo.isEmpty()) {
          sendMessage(
              chatId,
              "차량 등록 정보가 없습니다. /start 를 눌러 등록해주세요.");
        } else {
          const QString dbName = userInfo["name"].toString();
          const QString dbPlate = userInfo["plate_number"].toString();
          const QString dbPhone = userInfo["phone"].toString();
          const QString dbCard = userInfo["payment_info"].toString();

          // 카드번호 마스킹 (앞 4자리와 뒤 4자리를 표시하여 수정 여부 확인
          // 가능하게 개선)
          QString maskedCard = dbCard;
          if (dbCard.length() >= 8) {
            maskedCard = dbCard.left(4) + QString(dbCard.length() - 8, '*') +
                         dbCard.right(4);
          } else if (dbCard.length() >= 4) {
            maskedCard = dbCard.left(4) + QString(dbCard.length() - 4, '*');
          }

          // 인라인 버튼: '수정하기'만
          QJsonObject btnEdit;
          btnEdit["text"] = QString::fromUtf8("✏️ 수정하기");
          btnEdit["callback_data"] = "edit_select";

          QJsonArray inlineRow1;
          inlineRow1.append(btnEdit);
          QJsonArray inlineKeyboard;
          inlineKeyboard.append(inlineRow1);
          QJsonObject inlineMarkup;
          inlineMarkup["inline_keyboard"] = inlineKeyboard;
          QString inlineReplyMarkup = QString::fromUtf8(
              QJsonDocument(inlineMarkup).toJson(QJsonDocument::Compact));

          sendMessage(chatId,
                      QString::fromUtf8("👤 *내 정보*\n\n"
                                        "이름: %1\n"
                                        "차량번호: `%2`\n"
                                        "전화번호: %3\n"
                                        "결제 카드: `%4`")
                          .arg(dbName.isEmpty() ? "(미등록)" : dbName)
                          .arg(dbPlate.isEmpty() ? "(없음)" : dbPlate)
                          .arg(dbPhone.isEmpty() ? "(미등록)" : dbPhone)
                          .arg(maskedCard.isEmpty() ? "(미등록)" : maskedCard),
                      inlineReplyMarkup);
        }
      } else if (text.contains(QString::fromUtf8("요금 조회"))) {
        handleFeeInquiry(chatId);
      } else if (text == QString::fromUtf8("🕒 최근 이용 내역")) {
        handleUsageHistory(chatId);
      } else if (text == QString::fromUtf8("🚨 관리자 호출")) {
        // 즉시 호출 (확인 단계 없음)
        sendMessage(chatId, "✅ *관리자가 호출되었습니다.*\n잠시만 "
                            "기다려주시면 조치해 드리겠습니다.");
        emit logMessage(QString("[Telegram] 🚨 관리자 호출 요청! (User: %1)")
                            .arg(firstName));
        emit adminSummoned(chatId, firstName);
        sendMainMenu(chatId);
      } else if (text == QString::fromUtf8("◀ 메뉴로 돌아가기")) {
        sendMainMenu(chatId);
      } else {
        // LLM 자연어 처리 (기존 버튼/세션에 해당하지 않는 경우)
        ocr::recognition::TelegramLlmRunner::CommandCategory category = m_llmRunner->classifyCommand(text);
        if (category != ocr::recognition::TelegramLlmRunner::CommandCategory::NONE) {
          handleLlmCommand(chatId, text, category);
        } else {
          sendMainMenu(chatId, "죄송합니다. 명령을 이해하지 못했습니다.\n하단 메뉴 버튼을 사용하시거나 더 구체적으로 말씀해주세요.");
        }
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

void TelegramBotAPI::handleLlmCommand(const QString &chatId, const QString &userText,
                                      ocr::recognition::TelegramLlmRunner::CommandCategory category) {
  using Cat = ocr::recognition::TelegramLlmRunner::CommandCategory;
  
  switch (category) {
    case Cat::INFO_INQUIRY:  handleInfoInquiry(chatId); break;
    case Cat::FEE_INQUIRY:   handleFeeInquiry(chatId); break;
    case Cat::USAGE_HISTORY: handleUsageHistory(chatId); break;
    case Cat::CALL_ADMIN:    handleCallAdmin(chatId); break;
    case Cat::FEE_PAYMENT:   handleFeePayment(chatId); break;
    default:
      sendMainMenu(chatId);
      break;
  }
}

void TelegramBotAPI::handleInfoInquiry(const QString &chatId) {
  // 기존 "내 정보 조회" 버튼 로직과 동일하게 처리 (코드 중복 방지를 위해 공통화 가능하지만 일단 직접 구현)
  UserRepository userRepo;
  const QVector<QJsonObject> allUsers = userRepo.getAllUsersFull();
  QJsonObject userInfo;
  for (const QJsonObject &u : allUsers) {
    if (u["chat_id"].toString() == chatId) {
      userInfo = u;
      break;
    }
  }

  if (userInfo.isEmpty()) {
    sendMessage(chatId, "차량 등록 정보가 없습니다. /start 를 눌러 등록해주세요.");
  } else {
    QString maskedCard = userInfo["payment_info"].toString();
    if (maskedCard.length() >= 8) {
      maskedCard = maskedCard.left(4) + QString(maskedCard.length() - 8, '*') + maskedCard.right(4);
    }
    
    QJsonObject btnEdit;
    btnEdit["text"] = QString::fromUtf8("✏️ 수정하기");
    btnEdit["callback_data"] = "edit_select";
    QJsonArray row; row.append(btnEdit);
    QJsonArray keyboard; keyboard.append(row);
    QJsonObject markup; markup["inline_keyboard"] = keyboard;
    QString replyMarkup = QJsonDocument(markup).toJson(QJsonDocument::Compact);

    sendMessage(chatId,
                QString::fromUtf8("👤 *내 정보*\n\n이름: %1\n차량번호: `%2`\n전화번호: %3\n결제 카드: `%4`")
                    .arg(userInfo["name"].toString())
                    .arg(userInfo["plate_number"].toString())
                    .arg(userInfo["phone"].toString())
                    .arg(maskedCard),
                replyMarkup);
  }
}

void TelegramBotAPI::handleFeeInquiry(const QString &chatId) {
  const QString plate = m_chatToPlate.value(chatId).trimmed();
  if (plate.isEmpty()) {
    sendMessage(chatId, "등록된 차량이 없습니다.");
    return;
  }

  auto formatTime = [](const QString &isoText) {
    const QDateTime parsed = QDateTime::fromString(isoText, Qt::ISODate);
    if (parsed.isValid()) {
      return parsed.toString("yyyy-MM-dd HH:mm");
    }
    return isoText.left(16).replace('T', ' ');
  };

  const QJsonObject active = m_parkingRepository.findActiveByPlateAnyCamera(plate);
  if (!active.isEmpty()) {
    const QString entryTimeText = active["entry_time"].toString();
    const QDateTime entryTime = QDateTime::fromString(entryTimeText, Qt::ISODate);
    const parking::ParkingFeeResult feeResult =
        parking::calculateParkingFee(entryTime, QDateTime::currentDateTime());
    const QString cameraKey = active["camera_key"].toString().trimmed();
    const QString zoneName = active["zone_name"].toString().trimmed().isEmpty()
                                 ? QStringLiteral("-")
                                 : active["zone_name"].toString().trimmed();
    const QString feeText =
        feeResult.totalAmount > 0
            ? QStringLiteral("%1원").arg(feeResult.totalAmount)
            : QStringLiteral("무료");
    QString text = QString("💳 *현재 요금 조회*\n\n"
                           "차량번호: `%1`\n"
                           "채널: %2\n"
                           "구역: %3\n"
                           "입차 시간: %4\n"
                           "이용 시간: %5분\n"
                           "현재 요금: *%6*\n"
                           "출차 시점에 최종 요금이 확정됩니다.")
                       .arg(plate)
                       .arg(cameraKey.isEmpty() ? QStringLiteral("-") : cameraKey)
                       .arg(zoneName)
                       .arg(formatTime(entryTimeText))
                       .arg(feeResult.totalMinutes)
                       .arg(feeText);
    sendMessage(chatId, text);
    return;
  }

  const QJsonObject pendingLog =
      m_parkingRepository.findLatestUnpaidExitedByPlateAnyCamera(plate);
  if (!pendingLog.isEmpty()) {
    const int recordId = pendingLog["id"].toInt();
    const int fee = pendingLog["total_amount"].toInt();
    const QString exitTime = pendingLog["exit_time"].toString();
    const QString cameraKey = pendingLog["camera_key"].toString().trimmed();
    const QString zoneName = pendingLog["zone_name"].toString().trimmed().isEmpty()
                                 ? QStringLiteral("-")
                                 : pendingLog["zone_name"].toString().trimmed();
    QString text = QString("💳 *미납 정산 안내*\n\n"
                           "현재 주차 중인 내역은 없습니다.\n\n"
                           "차량번호: `%1`\n"
                           "채널: %2\n"
                           "구역: %3\n"
                           "출차 시간: %4\n"
                           "미납 금액: *%5원*")
                       .arg(plate)
                       .arg(cameraKey.isEmpty() ? QStringLiteral("-") : cameraKey)
                       .arg(zoneName)
                       .arg(formatTime(exitTime))
                       .arg(fee);

    if (fee > 0) {
      QJsonObject btnPay;
      btnPay["text"] = QString::fromUtf8("💰 납부하기");
      btnPay["callback_data"] = QString("PAYREC_%1").arg(recordId);
      QJsonArray row;
      row.append(btnPay);
      QJsonArray keyboard;
      keyboard.append(row);
      QJsonObject markup;
      markup["inline_keyboard"] = keyboard;
      const QString replyMarkup =
          QJsonDocument(markup).toJson(QJsonDocument::Compact);
      sendMessage(chatId, text, replyMarkup);
    } else {
      sendMessage(chatId, text);
    }
  } else {
    QString text = QString("💳 *요금 조회*\n\n"
                           "차량번호: `%1`\n"
                           "현재 주차 중인 내역이 없습니다.")
                       .arg(plate);
    sendMessage(chatId, text);
  }
}

void TelegramBotAPI::handleUsageHistory(const QString &chatId) {
  const QString plate = m_chatToPlate.value(chatId).trimmed();
  if (plate.isEmpty()) {
    sendMessage(chatId, "등록된 차량이 없습니다.");
    return;
  }

  const QList<QJsonObject> logs =
      m_parkingRepository.findLogsByExactPlateAnyCamera(plate);
  auto formatTime = [](const QString &isoText) {
    const QDateTime parsed = QDateTime::fromString(isoText, Qt::ISODate);
    if (parsed.isValid()) {
      return parsed.toString("yyyy-MM-dd HH:mm");
    }
    return isoText.left(16).replace('T', ' ');
  };

  QString text = "🕒 *최근 이용 내역 (최대 3건)*\n\n";
  int count = 0;
  for (const QJsonObject &log : logs) {
    const QString entryTime = log["entry_time"].toString().trimmed();
    const QString exitTime = log["exit_time"].toString().trimmed();
    if (entryTime.isEmpty() || exitTime.isEmpty()) {
      continue;
    }

    const QString zoneName = log["zone_name"].toString().trimmed().isEmpty()
                                 ? QStringLiteral("-")
                                 : log["zone_name"].toString().trimmed();
    const QString cameraKey = log["camera_key"].toString().trimmed().isEmpty()
                                  ? QStringLiteral("-")
                                  : log["camera_key"].toString().trimmed();
    text += QString("%1. %2\n"
                    "채널: %3\n"
                    "구역: %4\n"
                    "입차: %5\n"
                    "출차: %6\n"
                    "요금: %7원\n\n")
                .arg(count + 1)
                .arg(formatTime(entryTime).left(10))
                .arg(cameraKey)
                .arg(zoneName)
                .arg(formatTime(entryTime))
                .arg(formatTime(exitTime))
                .arg(log["total_amount"].toInt());
    count++;
    if (count >= 3) {
      break;
    }
  }

  if (count == 0) {
    sendMessage(chatId, "완료된 이용 내역이 없습니다.");
  } else {
    sendMessage(chatId, text.trimmed());
  }
}

void TelegramBotAPI::handleCallAdmin(const QString &chatId) {
  QString name = m_registrationSessions.contains(chatId) ? m_registrationSessions[chatId].name : "사용자";
  sendMessage(chatId, "✅ *관리자가 호출되었습니다.*\n잠시만 기다려주시면 조치해 드리겠습니다.");
  emit logMessage(QString("[Telegram] 🚨 LLM 관리자 호출 요청! (User: %1)").arg(name));
  emit adminSummoned(chatId, name);
}

void TelegramBotAPI::handleFeePayment(const QString &chatId) {
  const QString plate = m_chatToPlate.value(chatId).trimmed();
  if (plate.isEmpty()) {
    sendMessage(chatId, "등록된 차량이 없습니다.");
    return;
  }

  const QJsonObject pendingLog =
      m_parkingRepository.findLatestUnpaidExitedByPlateAnyCamera(plate);
  if (pendingLog.isEmpty()) {
    sendMessage(chatId, "현재 납부할 요금이 있는 주차 내역이 없습니다.");
  } else {
    handleFeeInquiry(chatId); // 요금 조회를 먼저 보여주어 결제 버튼 유도
  }
}
