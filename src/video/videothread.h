
#ifndef VIDEOTHREAD_H
#define VIDEOTHREAD_H

#include <QImage>
#include <QMutex>
#include <QSharedPointer>
#include <QThread>
#include <opencv2/opencv.hpp>


class VideoThread : public QThread {
  Q_OBJECT
public:
  explicit VideoThread(QObject *parent = nullptr);
  ~VideoThread();

  void setUrl(const QString &url);
  void stop();

signals:
  void frameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);

protected:
  void run() override;

private:
  QString m_url;
  bool m_stop;
  QMutex m_mutex;
  cv::VideoCapture m_cap;
};

Q_DECLARE_METATYPE(QSharedPointer<cv::Mat>)

#endif // VIDEOTHREAD_H
