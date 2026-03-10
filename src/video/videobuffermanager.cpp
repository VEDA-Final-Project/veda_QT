#include "video/videobuffermanager.h"
#include <algorithm>

VideoBufferManager::VideoBufferManager(int maxFrames, QObject *parent)
    : QObject(parent), m_maxFrames(maxFrames) {}

void VideoBufferManager::addFrame(QSharedPointer<cv::Mat> frame) {
  if (!frame || frame->empty())
    return;

  QMutexLocker locker(&m_mutex);
  m_buffer.push_back(frame);
  m_totalFramesAdded++;
  if (m_buffer.size() > static_cast<size_t>(m_maxFrames)) {
    m_buffer.pop_front();
  }
}

std::vector<QSharedPointer<cv::Mat>> VideoBufferManager::getFrames() const {
  QMutexLocker locker(&m_mutex);
  return std::vector<QSharedPointer<cv::Mat>>(m_buffer.begin(), m_buffer.end());
}

std::vector<QSharedPointer<cv::Mat>>
VideoBufferManager::getFrames(int preSec, int postSec, int fps) const {
  QMutexLocker locker(&m_mutex);
  int maxFrames = (preSec + postSec) * fps;
  if (maxFrames <= 0 || m_buffer.empty()) {
    return {};
  }
  // 가장 최근 N 프레임 반환 (구간 전후 설정 반영)
  int count = std::min(maxFrames, static_cast<int>(m_buffer.size()));
  auto it = m_buffer.end() - count;
  return std::vector<QSharedPointer<cv::Mat>>(it, m_buffer.end());
}

std::vector<QSharedPointer<cv::Mat>>
VideoBufferManager::getFramesSince(uint64_t startIdx) const {
  QMutexLocker locker(&m_mutex);
  if (startIdx >= m_totalFramesAdded || m_buffer.empty()) {
    return {};
  }

  uint64_t availableCount = m_totalFramesAdded - startIdx;
  if (availableCount > m_buffer.size()) {
    availableCount = m_buffer.size();
  }

  auto it = m_buffer.end() - availableCount;
  return std::vector<QSharedPointer<cv::Mat>>(it, m_buffer.end());
}

uint64_t VideoBufferManager::getTotalFramesAdded() const {
  QMutexLocker locker(&m_mutex);
  return m_totalFramesAdded;
}

void VideoBufferManager::clear() {
  QMutexLocker locker(&m_mutex);
  m_buffer.clear();
}
