#ifndef OCR_PREPROCESS_PLATEPREPROCESSOR_H
#define OCR_PREPROCESS_PLATEPREPROCESSOR_H

#include <QString>
#include <opencv2/opencv.hpp>
#include <vector>

namespace ocr::preprocess
{

struct OcrInputVariant
{
  QString tag;
  cv::Mat imageRgb;
};

} // namespace ocr::preprocess

#endif // OCR_PREPROCESS_PLATEPREPROCESSOR_H
