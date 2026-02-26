#ifndef OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
#define OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H

#include "ocr/ocrmanager.h"
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
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

  const char *plateWhitelist();
  const char *hangulWhitelist();
  tesseract::PageSegMode primaryPageSegMode();
  tesseract::PageSegMode secondaryPageSegMode();
  QString psmTag(tesseract::PageSegMode psm);
  QString normalizePlateText(const QString &raw);
  int platePlausibilityScore(const QString &candidate);
  OcrResult chooseBestPlateResult(const std::vector<OcrCandidate> &candidates);
  QString recoverPlateUsingCenterHangul(tesseract::TessBaseAPI *api,
                                        const cv::Mat &binary,
                                        const cv::Mat &binaryInv,
                                        const QString &selectedCandidate);

} // namespace ocr::postprocess

#endif // OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
