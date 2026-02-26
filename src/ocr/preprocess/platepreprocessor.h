#ifndef OCR_PREPROCESS_PLATEPREPROCESSOR_H
#define OCR_PREPROCESS_PLATEPREPROCESSOR_H

#include <opencv2/opencv.hpp>

namespace ocr::preprocess
{

  // OCR 전처리 결과 묶음.
  // 디버그용 컬러/오버레이, OCR 입력용 gray/binary 계열, 적용 여부 플래그를 함께 전달한다.
  struct PreprocessOutput
  {
    cv::Mat roiDebugRgb;
    cv::Mat ocrGray;
    cv::Mat ocrRgb;
    cv::Mat quadDebug;
    cv::Mat binary;
    cv::Mat binaryInv;
    cv::Mat adaptiveBinary;
    bool adaptiveUsed = false;
    bool perspectiveApplied = false;
  };

  // 번호판 ROI를 OCR용 이미지로 전처리한다.
  // 내부에서 원근 보정, 업스케일, Otsu/Adaptive 이진화를 수행한다.
  bool preprocessPlateRoi(const cv::Mat &roiRgb, PreprocessOutput *out);

} // namespace ocr::preprocess

#endif // OCR_PREPROCESS_PLATEPREPROCESSOR_H
