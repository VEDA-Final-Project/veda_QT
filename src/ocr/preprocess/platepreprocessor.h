#ifndef PLATEPREPROCESSOR_H
#define PLATEPREPROCESSOR_H

#include <QImage>
#include <QString>
#include <opencv2/core.hpp>

namespace ocr::preprocess
{

struct PlatePreprocessResult
{
  cv::Mat roiRgb;
  cv::Mat normalizedRgb;
  cv::Mat enhancedGray;
  cv::Mat ocrInputRgb;
  QString dropReason;
};

bool preprocessPlateImage(const QImage &image, int inputWidth, int inputHeight,
                          PlatePreprocessResult *resultOut);

} // namespace ocr::preprocess

#endif // PLATEPREPROCESSOR_H
