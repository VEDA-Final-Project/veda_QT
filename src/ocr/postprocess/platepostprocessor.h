#ifndef OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
#define OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H

#include "ocr/ocrtypes.h"
#include <QString>
#include <vector>

namespace ocr::postprocess
{

struct OcrCandidate
{
  QString rawText;
  QString normalizedText;
  int score = 0;
  int confidence = -1;
};

QString normalizePlateTextWithConfusableFix(const QString &raw);
OcrResult chooseBestPlateResult(const OcrCandidate &candidate);

} // namespace ocr::postprocess

#endif // OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
