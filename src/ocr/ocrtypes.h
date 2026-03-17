#ifndef OCR_OCRTYPES_H
#define OCR_OCRTYPES_H

#include <QString>

struct OcrResult {
  QString text;
  QString selectedRawText;
};

struct OcrFullResult {
  QString raw;
  QString filtered;
  int latencyMs;
};

#endif // OCR_OCRTYPES_H
