#include "telegramllmrunner.h"
#include "config/config.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QDebug>

namespace ocr::recognition {

TelegramLlmRunner::TelegramLlmRunner(QObject *parent) : QObject(parent) {}

TelegramLlmRunner::~TelegramLlmRunner() {}

TelegramLlmRunner::CommandCategory TelegramLlmRunner::classifyCommand(const QString &userText) {
  if (userText.isEmpty()) {
    return CommandCategory::NONE;
  }

  const QString apiKey = Config::instance().geminiApiKey();
  if (apiKey.isEmpty()) {
    qWarning() << "[Telegram LLM] API Key is missing!";
    return CommandCategory::NONE;
  }

  const QString model = Config::instance().geminiModel();
  const QString urlStr = QString("https://generativelanguage.googleapis.com/"
                                 "v1beta/models/%1:generateContent?key=%2")
                             .arg(model, apiKey);

  const QString jsonPayload = buildJsonRequest(userText);

  QNetworkRequest request((QUrl(urlStr)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QNetworkAccessManager manager;
  QEventLoop loop;
  QNetworkReply *reply = manager.post(request, jsonPayload.toUtf8());
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  CommandCategory category = CommandCategory::NONE;
  if (reply->error() == QNetworkReply::NoError) {
    category = parseResponse(reply->readAll());
  } else {
    qWarning() << "[Telegram LLM] API Error:" << reply->errorString();
  }

  reply->deleteLater();
  return category;
}

QString TelegramLlmRunner::buildJsonRequest(const QString &userText) const {
  QString prompt = QString(
      "You are a parking management system bot. Categorize the user's input into exactly one of these categories: "
      "INFO_INQUIRY, FEE_INQUIRY, USAGE_HISTORY, CALL_ADMIN, FEE_PAYMENT, NONE. "
      "User input can be in Korean or English. "
      "Categories description: "
      "- INFO_INQUIRY: User wants to check their profile, car number, or personal info. (e.g., '내 정보', '내 차 번호가 뭐야?') "
      "- FEE_INQUIRY: User wants to know current parking fee or usage time. (e.g., '요금 얼마야?', '몇 분 주차했어?') "
      "- USAGE_HISTORY: User wants to see past parking records. (e.g., '이용 내역', '언제 주차했지?') "
      "- CALL_ADMIN: User wants to talk to a human or needs emergency help. (e.g., '관리자 호출', '도와주세요', '사람 불러줘') "
      "- FEE_PAYMENT: User wants to pay the fee now. (e.g., '결제할래', '돈 낼게') "
      "- NONE: Anything else. "
      "Respond ONLY with the category name in plain text. "
      "User Input: \"%1\"").arg(userText);

  QJsonObject content;
  QJsonArray parts;
  QJsonObject textPart;
  textPart["text"] = prompt;
  parts.append(textPart);
  content["parts"] = parts;

  QJsonArray contents;
  contents.append(content);

  QJsonObject root;
  root["contents"] = contents;

  return QJsonDocument(root).toJson();
}

TelegramLlmRunner::CommandCategory TelegramLlmRunner::parseResponse(const QByteArray &response) const {
  QJsonDocument doc = QJsonDocument::fromJson(response);
  QJsonObject root = doc.object();
  QJsonArray candidates = root["candidates"].toArray();

  if (!candidates.isEmpty()) {
    QJsonObject candidate = candidates[0].toObject();
    QJsonObject content = candidate["content"].toObject();
    QJsonArray parts = content["parts"].toArray();
    if (!parts.isEmpty()) {
      QString result = parts[0].toObject()["text"].toString().trimmed().toUpper();
      
      if (result.contains("INFO_INQUIRY")) return CommandCategory::INFO_INQUIRY;
      if (result.contains("FEE_INQUIRY")) return CommandCategory::FEE_INQUIRY;
      if (result.contains("USAGE_HISTORY")) return CommandCategory::USAGE_HISTORY;
      if (result.contains("CALL_ADMIN")) return CommandCategory::CALL_ADMIN;
      if (result.contains("FEE_PAYMENT")) return CommandCategory::FEE_PAYMENT;
    }
  }

  return CommandCategory::NONE;
}

} // namespace ocr::recognition
