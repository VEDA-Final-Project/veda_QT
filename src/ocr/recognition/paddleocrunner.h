#ifndef OCR_RECOGNITION_PADDLEOCRRUNNER_H
#define OCR_RECOGNITION_PADDLEOCRRUNNER_H

#include "ocr/postprocess/platepostprocessor.h"
#include <onnxruntime_cxx_api.h>
#include <QString>
#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ocr::recognition
{

class PaddleOcrRunner
{
public:
  bool init(const QString &modelPath, const QString &dictPath, int inputWidth,
            int inputHeight, QString *errorOut = nullptr);
  bool isReady() const;

  postprocess::OcrCandidate runSingleCandidate(const cv::Mat &imageRgb);

private:
  bool loadDictionary(const QString &dictPath, QString *errorOut);
  static std::vector<QString> fallbackDictionary();

  postprocess::OcrCandidate runOcrOnImage(const cv::Mat &imageRgb);
  cv::Mat makeInputBlob(const cv::Mat &imageRgb) const;

  QString decodeCtcGreedy(const cv::Mat &scores, int classCount,
                          int *confidenceOut) const;
  static QString normalizeSingleCandidate(const QString &raw);

  std::unique_ptr<Ort::Env> m_env;
  std::unique_ptr<Ort::Session> m_session;
  std::vector<std::string> m_inputNameStorage;
  std::vector<std::string> m_outputNameStorage;
  std::vector<const char *> m_inputNames;
  std::vector<const char *> m_outputNames;
  std::vector<QString> m_dictionary;
  int m_inputWidth = 320;
  int m_inputHeight = 48;
  bool m_ready = false;
};

} // namespace ocr::recognition

#endif // OCR_RECOGNITION_PADDLEOCRRUNNER_H
