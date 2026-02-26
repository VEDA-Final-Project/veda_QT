#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include <QImage>
#include <QString>
#include <leptonica/allheaders.h>
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>

// OCR helper that wraps Tesseract and is safe to use from a worker thread.
// Not a QObject: avoid cross-thread QObject affinity issues.
struct OcrResult
{
  QString text;              // Final accepted text (empty when undecided)
  QString selectedCandidate; // Best normalized candidate before final gating
  int selectedScore = 0;
  int selectedConfidence = -1;
  bool confidenceTiebreakUsed = false;
  QString dropReason; // Non-empty when result is dropped/held
};

class OcrManager
{
public:
  OcrManager();
  ~OcrManager();

  bool init(const QString &datapath = QString(), const QString &language = "eng");
  OcrResult performOcrDetailed(const QImage &image);
  QString performOcr(const QImage &image);

private:
  tesseract::TessBaseAPI *m_tessApi;
};

#endif // OCRMANAGER_H
