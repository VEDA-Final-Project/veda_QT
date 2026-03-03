#ifndef OCR_OCRTYPES_H
#define OCR_OCRTYPES_H

#include <QString>

struct OcrResult
{
  QString text;
  QString selectedRawText;
  QString selectedCandidate;
  int selectedScore = 0;
  int selectedConfidence = -1;
};

#endif // OCR_OCRTYPES_H
