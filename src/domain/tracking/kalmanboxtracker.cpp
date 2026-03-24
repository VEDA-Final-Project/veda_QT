#include "domain/tracking/kalmanboxtracker.h"

#include <QtGlobal>

namespace tracking {

KalmanBoxTracker::KalmanBoxTracker() : m_filter(8, 4, 0, CV_32F) {
  cv::setIdentity(m_filter.transitionMatrix);
  for (int i = 0; i < 4; ++i) {
    m_filter.transitionMatrix.at<float>(i, i + 4) = 1.0f;
  }

  m_filter.measurementMatrix = cv::Mat::zeros(4, 8, CV_32F);
  for (int i = 0; i < 4; ++i) {
    m_filter.measurementMatrix.at<float>(i, i) = 1.0f;
  }

  cv::setIdentity(m_filter.processNoiseCov, cv::Scalar::all(1e-2));
  cv::setIdentity(m_filter.measurementNoiseCov, cv::Scalar::all(1e-1));
  cv::setIdentity(m_filter.errorCovPost, cv::Scalar::all(1.0));
}

void KalmanBoxTracker::init(const QRectF &box) {
  const cv::Rect2f rect = toCvRect(box);
  m_filter.statePost = cv::Mat::zeros(8, 1, CV_32F);
  m_filter.statePost.at<float>(0) = rect.x;
  m_filter.statePost.at<float>(1) = rect.y;
  m_filter.statePost.at<float>(2) = rect.width;
  m_filter.statePost.at<float>(3) = rect.height;
  m_lastPrediction = box;
  m_initialized = true;
}

QRectF KalmanBoxTracker::predict() {
  if (!m_initialized) {
    return QRectF();
  }
  const cv::Mat prediction = m_filter.predict();
  m_lastPrediction = toQRectF(prediction);
  return m_lastPrediction;
}

void KalmanBoxTracker::correct(const QRectF &box) {
  if (!m_initialized) {
    init(box);
    return;
  }

  const cv::Rect2f rect = toCvRect(box);
  cv::Mat measurement(4, 1, CV_32F);
  measurement.at<float>(0) = rect.x;
  measurement.at<float>(1) = rect.y;
  measurement.at<float>(2) = rect.width;
  measurement.at<float>(3) = rect.height;
  m_filter.correct(measurement);
  m_lastPrediction = box;
}

bool KalmanBoxTracker::isInitialized() const { return m_initialized; }

QRectF KalmanBoxTracker::lastPrediction() const { return m_lastPrediction; }

cv::Rect2f KalmanBoxTracker::toCvRect(const QRectF &box) {
  return cv::Rect2f(static_cast<float>(box.x()), static_cast<float>(box.y()),
                    static_cast<float>(box.width()),
                    static_cast<float>(box.height()));
}

QRectF KalmanBoxTracker::toQRectF(const cv::Mat &state) {
  const float x = state.at<float>(0);
  const float y = state.at<float>(1);
  const float w = qMax(0.0f, state.at<float>(2));
  const float h = qMax(0.0f, state.at<float>(3));
  return QRectF(x, y, w, h);
}

} // namespace tracking
