#ifndef REIDEXTRACTOR_H
#define REIDEXTRACTOR_H

#include <memory>
#include <opencv2/opencv.hpp>
#include <QString>
#include <vector>

class ReIDFeatureExtractor {
public:
  ReIDFeatureExtractor();
  ~ReIDFeatureExtractor();

  bool loadModel(const QString &modelPath);
  std::vector<float> extract(const cv::Mat &image);

private:
  struct Impl;
  std::unique_ptr<Impl> pimpl;
};

#endif // REIDEXTRACTOR_H
