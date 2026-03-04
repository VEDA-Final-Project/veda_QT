#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include <QImage>
#include <QString>
#include <leptonica/allheaders.h>
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>

// OCR helper that wraps Tesseract and is safe to use from a worker thread.
// Not a QObject: avoid cross-thread QObject affinity issues.
struct OcrFullResult {
  QString raw;      // Raw OCR from PSM_SINGLE_LINE (fallback or first)
  QString filtered; // Final E2E result
  int latencyMs;    // Processing time in milliseconds
};

class OcrManager {
public:
  OcrManager();
  ~OcrManager();

  bool init(const QString &datapath = QString(),
            const QString &language = "eng");
  OcrFullResult performOcr(const QImage &image);

  // Raw OCR: 이미지 원본 그대로(흑백 변환만 적용) Tesseract로 인식하여 순수
  // 엔진 성능 측정
  QString performRawOcr(const QImage &image);
  void setBenchmarkMode(bool enable) { m_isBenchmarkMode = enable; }

private:
  tesseract::TessBaseAPI *m_tessApi;
  bool m_isBenchmarkMode = false;
};

#endif // OCRMANAGER_H
