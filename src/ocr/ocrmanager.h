#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include "ocr/ocrtypes.h"
#include "ocr/recognition/paddleocrunner.h"
#include "ocr/recognition/llmocrrunner.h"
#include <QImage>
#include <QString>
#include <QStringList>

class OcrManager {
public:
  OcrManager();
  ~OcrManager();

  bool init(const QString &modelPath = QString(),
            const QString &dictPath = QString(), int inputWidth = 320,
            int inputHeight = 48);

  OcrResult performOcrDetailed(const QImage &image, int objectId = -1);
  OcrFullResult performOcr(const QImage &image, int objectId = -1);

private:
  static QString findFirstFileRecursively(const QString &rootPath,
                                          const QStringList &filters);
  static QString resolveFilePath(const QString &pathOrDir,
                                 const QStringList &filters);
  QString resolveModelPath(const QString &modelPath) const;
  QString resolveDictionaryPath(const QString &dictPath,
                                const QString &resolvedModelPath) const;

  ocr::recognition::PaddleOcrRunner m_runner;
  ocr::recognition::LlmOcrRunner *m_llmRunner = nullptr;
  int m_inputWidth = 320;
  int m_inputHeight = 48;
};

#endif // OCRMANAGER_H
