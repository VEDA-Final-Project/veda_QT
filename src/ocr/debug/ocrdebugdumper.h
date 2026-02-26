#ifndef OCR_DEBUG_OCRDEBUGDUMPER_H
#define OCR_DEBUG_OCRDEBUGDUMPER_H

#include <opencv2/opencv.hpp>

namespace ocr::debug
{

    // OCR 파이프라인 각 단계의 중간 결과 이미지를 디버그 폴더에 저장한다.
    // roi/rectified/quadDebug는 RGB 입력을 기대하며, 파일 저장 시 BGR로 변환된다.
    void dumpOcrStages(const cv::Mat &roiRgb, const cv::Mat &gray,
                       const cv::Mat &rectifiedRgb, const cv::Mat &quadDebugRgb,
                       const cv::Mat &binary, const cv::Mat &binaryInv);

} // namespace ocr::debug

#endif // OCR_DEBUG_OCRDEBUGDUMPER_H
