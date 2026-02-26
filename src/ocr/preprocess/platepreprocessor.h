#ifndef OCR_PREPROCESS_PLATEPREPROCESSOR_H
#define OCR_PREPROCESS_PLATEPREPROCESSOR_H

#include <opencv2/opencv.hpp>

namespace ocr::preprocess
{

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
    bool upscaleAfterWarp = false;
    bool perspectiveApplied = false;
  };

  bool preprocessPlateRoi(const cv::Mat &roiRgb, PreprocessOutput *out);

} // namespace ocr::preprocess

#endif // OCR_PREPROCESS_PLATEPREPROCESSOR_H
