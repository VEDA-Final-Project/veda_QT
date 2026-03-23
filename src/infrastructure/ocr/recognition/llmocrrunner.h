#ifndef LLMOCRRUNNER_H
#define LLMOCRRUNNER_H

#include "infrastructure/ocr/ocrtypes.h"
#include <QImage>
#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace ocr::recognition {

/**
 * @brief Google Gemini API를 사용하여 OCR을 수행하는 클래스
 */
class LlmOcrRunner : public QObject {
  Q_OBJECT

public:
  explicit LlmOcrRunner(QObject *parent = nullptr);
  ~LlmOcrRunner() override;

  /**
   * @brief 단일 이미지에 대해 OCR을 수행합니다 (동기 방식 시뮬레이션)
   * @param image 부위별 크롭된 이미지
   * @param objectId 디버그 기록용 객체 ID
   * @return OCR 결과
   */
  OcrResult runSingleCandidate(const QImage &image, int objectId = -1);

private:
  QString encodeImageToBase64(const QImage &image) const;
  QString buildJsonRequest(const QString &base64Image) const;
  QString parseResponse(const QByteArray &response) const;
  
  void saveDebugData(const QImage &image, const QString &result, int objectId, qint64 latencyMs);
  void rotateDebugFiles(const QString &dirPath, const QString &prefix, int keepCount);
};

} // namespace ocr::recognition

#endif // LLMOCRRUNNER_H
