#ifndef REIDEXTRACTOR_H
#define REIDEXTRACTOR_H

#include <QString>
#include <memory>
#include <vector>

#include <opencv2/opencv.hpp>

class ReidSession {
public:
  ReidSession();
  ~ReidSession();

  bool isReady() const;
  std::vector<float> extract(const cv::Mat &image);

private:
  struct Impl;
  explicit ReidSession(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> pimpl;

  friend class SharedReidRuntime;
};

class SharedReidRuntime {
public:
  struct Impl;

  SharedReidRuntime();
  ~SharedReidRuntime();

  bool loadOnce(const QString &modelPath);
  bool isReady() const;
  QString deviceName() const;
  QString lastError() const;

  std::shared_ptr<ReidSession> createSession() const;

private:
  std::shared_ptr<Impl> pimpl;
};

#endif // REIDEXTRACTOR_H
