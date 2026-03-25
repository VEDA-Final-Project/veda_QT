
#ifndef VIDEOTHREAD_H
#define VIDEOTHREAD_H

#include <QImage>
#include <QMutex>
#include <QSharedPointer>
#include <QString>
#include <QThread>
#include <opencv2/opencv.hpp>
#include <vector>

class VideoThread : public QThread {
  Q_OBJECT
public:
  explicit VideoThread(QObject *parent = nullptr);
  ~VideoThread();

  void setUrl(const QString &url);
  void setTargetFps(int fps); // 타겟 FPS 설정 (프레임 스킵용)
  void stop();
  double getActualFps() const; // 실제 스트림 FPS 반환

signals:
  void frameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);
  void logMessage(const QString &msg);

protected:
  void run() override;

private:
  QString m_url;
  int m_targetFps = 0; // 0이면 스킵 없음
  bool m_stop;
  mutable QMutex m_mutex;
  cv::VideoCapture m_cap;
  double m_actualFps = 15.0;
};

Q_DECLARE_METATYPE(QSharedPointer<cv::Mat>)
Q_DECLARE_METATYPE(std::vector<QSharedPointer<cv::Mat>>)

#endif // VIDEOTHREAD_H
