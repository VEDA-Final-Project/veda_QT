#ifndef OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
#define OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H

#include "ocr/ocrtypes.h"
#include <QString>
#include <vector>

namespace ocr::postprocess
{

struct OcrCandidate
{
  QString sourceTag;
  QString rawText;
  QString normalizedText;
  int score = 0;
  int confidence = -1;
};

QString normalizePlateText(const QString &raw);
QString normalizePlateTextWithConfusableFix(const QString &raw);
QString canonicalizePlateCandidate(const QString &normalized);
int platePlausibilityScore(const QString &candidate);
OcrResult chooseBestPlateResult(const std::vector<OcrCandidate> &candidates);

} // namespace ocr::postprocess

#endif // OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
