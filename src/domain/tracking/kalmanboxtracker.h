#ifndef DOMAIN_TRACKING_KALMANBOXTRACKER_H
#define DOMAIN_TRACKING_KALMANBOXTRACKER_H

#include <QRectF>
#include <opencv2/video/tracking.hpp>

namespace tracking {

class KalmanBoxTracker {
public:
  KalmanBoxTracker();

  void init(const QRectF &box);
  QRectF predict();
  void correct(const QRectF &box);
  bool isInitialized() const;
  QRectF lastPrediction() const;

private:
  static cv::Rect2f toCvRect(const QRectF &box);
  static QRectF toQRectF(const cv::Mat &state);

  cv::KalmanFilter m_filter;
  QRectF m_lastPrediction;
  bool m_initialized = false;
};

} // namespace tracking

#endif // DOMAIN_TRACKING_KALMANBOXTRACKER_H
