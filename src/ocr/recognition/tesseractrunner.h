#ifndef OCR_RECOGNITION_TESSERACTRUNNER_H
#define OCR_RECOGNITION_TESSERACTRUNNER_H

#include "ocr/postprocess/platepostprocessor.h"
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <vector>

namespace ocr::recognition
{

    // 단일 이진 영상(binary)으로 OCR을 1회 실행해 후보를 반환한다.
    // sourceTag/psm 정보를 함께 담아 후처리 단계에서 비교 가능하게 한다.
    postprocess::OcrCandidate runOcrOnBinary(tesseract::TessBaseAPI *api,
                                             const cv::Mat &binary,
                                             const QString &sourceTag,
                                             tesseract::PageSegMode psm);

    // binary/binary_inv/(옵션)adaptive 입력에 대해 다중 후보를 수집한다.
    // 각 입력은 line/word PSM을 모두 시도한다.
    std::vector<postprocess::OcrCandidate> collectCandidates(
        tesseract::TessBaseAPI *api, const cv::Mat &binary, const cv::Mat &binaryInv,
        const cv::Mat &adaptiveBinary, bool adaptiveUsed);

} // namespace ocr::recognition

#endif // OCR_RECOGNITION_TESSERACTRUNNER_H
