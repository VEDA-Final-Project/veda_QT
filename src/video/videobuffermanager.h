#ifndef VIDEOBUFFERMANAGER_H
#define VIDEOBUFFERMANAGER_H

#include <QMutex>
#include <QObject>
#include <QSharedPointer>
#include <deque>
#include <opencv2/opencv.hpp>
#include <vector>

class VideoBufferManager : public QObject {
  Q_OBJECT
public:
  explicit VideoBufferManager(
      int maxFrames = 150,
      QObject *parent = nullptr); // 기본 150프레임 (약 5~10초)
  void addFrame(QSharedPointer<cv::Mat> frame);

  // 전체 버퍼 반환
  std::vector<QSharedPointer<cv::Mat>> getFrames() const;
  // 이벤트 구간 버퍼 반환 (preSec + postSec 초 분량 프레임)
  std::vector<QSharedPointer<cv::Mat>> getFrames(int preSec, int postSec,
                                                 int fps = 15) const;
  // 수동 녹화를 위한 특정 시점 이후의 프레임들만 반환
  std::vector<QSharedPointer<cv::Mat>> getFramesSince(uint64_t startIdx) const;

  uint64_t getTotalFramesAdded() const;

  void clear();

private:
  int m_maxFrames;
  std::deque<QSharedPointer<cv::Mat>> m_buffer;
  mutable QMutex m_mutex;
  uint64_t m_totalFramesAdded = 0;
};

#endif // VIDEOBUFFERMANAGER_H
