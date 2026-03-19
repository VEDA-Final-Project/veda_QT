#include "llmocrrunner.h"
#include "config/config.h"
#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

namespace ocr::recognition {

LlmOcrRunner::LlmOcrRunner(QObject *parent) : QObject(parent) {}

LlmOcrRunner::~LlmOcrRunner() {}

OcrResult LlmOcrRunner::runSingleCandidate(const QImage &image, int objectId) {
  OcrResult result;

  if (image.isNull()) {
    result.text = "";
    return result;
  }

  const QString apiKey = Config::instance().geminiApiKey();
  if (apiKey.isEmpty()) {
    qWarning() << "[LLM OCR] API Key is missing!";
    result.text = "ERROR: NO API KEY";
    return result;
  }

  const QString model = Config::instance().geminiModel();
  const QString urlStr = QString("https://generativelanguage.googleapis.com/"
                                 "v1beta/models/%1:generateContent?key=%2")
                             .arg(model, apiKey);

  const QString base64Image = encodeImageToBase64(image);
  const QString jsonPayload = buildJsonRequest(base64Image);

  QNetworkRequest request((QUrl(urlStr)));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  // 응답 시간 측정 시작
  QElapsedTimer timer;
  timer.start();

  // 스레드 안전성을 위해 함수 내부에서 로컬 매니저 생성
  QNetworkAccessManager manager;
  QEventLoop loop;
  QNetworkReply *reply = manager.post(request, jsonPayload.toUtf8());
  connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();

  qint64 latencyMs = timer.elapsed();

  if (reply->error() == QNetworkReply::NoError) {
    QByteArray responseData = reply->readAll();
    result.text = parseResponse(responseData);
    result.selectedRawText = result.text;
  } else {
    QByteArray errorData = reply->readAll();
    QString errTitle = reply->errorString();
    // URL에 포함된 API 키 제거 (보안)
    int keyIdx = errTitle.indexOf("?key=");
    if (keyIdx != -1) {
      errTitle = errTitle.left(keyIdx) + "?key=********";
    }
    qWarning() << "[LLM OCR] API Error:" << errTitle;
    if (!errorData.isEmpty()) {
      qWarning() << "[LLM OCR] Error Response Body:" << errorData;
    }
    result.text = "";
  }

  saveDebugData(image, result.text, objectId, latencyMs);

  reply->deleteLater();
  return result;
}

QString LlmOcrRunner::encodeImageToBase64(const QImage &image) const {
  QByteArray ba;
  QBuffer buffer(&ba);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return ba.toBase64();
}

QString LlmOcrRunner::buildJsonRequest(const QString &base64Image) const {
  const QString prompt = Config::instance().geminiPrompt();

  QJsonObject content;
  QJsonArray parts;

  // Text part
  QJsonObject textPart;
  textPart["text"] = prompt;
  parts.append(textPart);

  // Image part
  QJsonObject imagePart;
  QJsonObject inlineData;
  inlineData["mime_type"] = "image/png";
  inlineData["data"] = base64Image;
  imagePart["inline_data"] = inlineData;
  parts.append(imagePart);

  content["parts"] = parts;

  QJsonArray contents;
  contents.append(content);

  QJsonObject root;
  root["contents"] = contents;

  return QJsonDocument(root).toJson();
}

QString LlmOcrRunner::parseResponse(const QByteArray &response) const {
  QJsonDocument doc = QJsonDocument::fromJson(response);
  QJsonObject root = doc.object();

  // candidates[0].content.parts[0].text 추출
  QJsonArray candidates = root["candidates"].toArray();
  if (!candidates.isEmpty()) {
    QJsonObject candidate = candidates[0].toObject();
    QJsonObject content = candidate["content"].toObject();
    QJsonArray parts = content["parts"].toArray();
    if (!parts.isEmpty()) {
      QString text = parts[0].toObject()["text"].toString().trimmed();
      // 숫자와 한글만 남기기 (필요시 정규식 추가 가능)
      return text;
    }
  }

  qWarning() << "[LLM OCR] Failed to parse response:" << response;
  return "";
}

void LlmOcrRunner::saveDebugData(const QImage &image, const QString &result,
                                 int objectId, qint64 latencyMs) {
  const QString debugDir = QDir::currentPath() + "/ocr_debug/llm";
  QDir().mkpath(debugDir);

  const QString stamp =
      QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");

  // 결과 텍스트 정제 (파일명에 쓸 수 없는 문자 제거)
  QString sanitizedResult = result;
  sanitizedResult.remove(QRegularExpression("[^a-zA-Z0-9가-힣]"));
  if (sanitizedResult.isEmpty())
    sanitizedResult = "empty";

  // 파일명 형식: llm_obj_{id}_{stamp}_{result}_{latency}ms.png
  const QString fileName = QString("llm_obj_%1_%2_%3_%4ms.png")
                               .arg(objectId)
                               .arg(stamp)
                               .arg(sanitizedResult)
                               .arg(latencyMs);

  // 이미지 저장
  image.save(debugDir + "/" + fileName);

  // 파일 로테이션 (최근 20개 유지)
  rotateDebugFiles(debugDir, "llm_obj", 20);
}

void LlmOcrRunner::rotateDebugFiles(const QString &dirPath,
                                    const QString &prefix, int keepCount) {
  QDir dir(dirPath);
  QStringList filters;
  filters << prefix + "_*.png" << prefix + "_*.txt";

  QFileInfoList files =
      dir.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);
  if (files.size() > keepCount) {
    for (int i = keepCount; i < files.size(); ++i) {
      QFile::remove(files[i].absoluteFilePath());
    }
  }
}

} // namespace ocr::recognition
