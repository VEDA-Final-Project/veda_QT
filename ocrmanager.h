#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include <QImage>
#include <QObject>
#include <leptonica/allheaders.h>
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>


class OcrManager : public QObject {
  Q_OBJECT
public:
  explicit OcrManager(QObject *parent = nullptr);
  ~OcrManager();

  bool init(const char *datapath = nullptr, const char *language = "eng");
  QString performOcr(const QImage &image);

private:
  tesseract::TessBaseAPI *m_tessApi;
};

#endif // OCRMANAGER_H
