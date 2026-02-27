#ifndef OCR_DEBUG_OCRDEBUGDUMPER_H
#define OCR_DEBUG_OCRDEBUGDUMPER_H

#include "ocr/preprocess/platepreprocessor.h"
#include <opencv2/opencv.hpp>

namespace ocr::debug
{

void dumpOcrStages(const cv::Mat &roiRgb, const cv::Mat &normalizedRgb,
                   const cv::Mat &enhancedGray,
                   const std::vector<ocr::preprocess::OcrInputVariant> &variants);

} // namespace ocr::debug

#endif // OCR_DEBUG_OCRDEBUGDUMPER_H
