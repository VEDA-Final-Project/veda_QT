#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include <QImage>
#include <QString>
#include <leptonica/allheaders.h>
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>


// OCR helper that wraps Tesseract and is safe to use from a worker thread.
// Not a QObject: avoid cross-thread QObject affinity issues.
class OcrManager {
public:
  OcrManager();
  ~OcrManager();

  bool init(const QString &datapath = QString(), const QString &language = "eng");
  QString performOcr(const QImage &image);

private:
  tesseract::TessBaseAPI *m_tessApi;
};

#endif // OCRMANAGER_H
