#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include "ocr/ocrtypes.h"
#include "ocr/recognition/llmocrrunner.h"
#include <QImage>
#include <QString>
#include <QStringList>

class OcrManager {
public:
  OcrManager();
  ~OcrManager();

  bool init();

  OcrFullResult performOcr(const QImage &image, int objectId = -1);

private:
  ocr::recognition::LlmOcrRunner *m_llmRunner = nullptr;
};

#endif // OCRMANAGER_H
