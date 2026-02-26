#ifndef OCR_RECOGNITION_TESSERACTRUNNER_H
#define OCR_RECOGNITION_TESSERACTRUNNER_H

#include "ocr/postprocess/platepostprocessor.h"
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <vector>

namespace ocr::recognition
{

postprocess::OcrCandidate runOcrOnBinary(tesseract::TessBaseAPI *api,
                                         const cv::Mat &binary,
                                         const QString &sourceTag,
                                         tesseract::PageSegMode psm);

std::vector<postprocess::OcrCandidate> collectCandidates(
    tesseract::TessBaseAPI *api, const cv::Mat &binary, const cv::Mat &binaryInv,
    const cv::Mat &adaptiveBinary, bool adaptiveUsed);

} // namespace ocr::recognition

#endif // OCR_RECOGNITION_TESSERACTRUNNER_H
