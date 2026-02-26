#ifndef OCR_DEBUG_OCRDEBUGDUMPER_H
#define OCR_DEBUG_OCRDEBUGDUMPER_H

#include <opencv2/opencv.hpp>

namespace ocr::debug
{

    void dumpOcrStages(const cv::Mat &roiRgb, const cv::Mat &gray,
                       const cv::Mat &rectifiedRgb, const cv::Mat &quadDebugRgb,
                       const cv::Mat &binary, const cv::Mat &binaryInv);

} // namespace ocr::debug

#endif // OCR_DEBUG_OCRDEBUGDUMPER_H
