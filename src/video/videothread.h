
#ifndef VIDEOTHREAD_H
#define VIDEOTHREAD_H

#include <QThread>
#include <QImage>
#include <QMutex>
#include <opencv2/opencv.hpp>

class VideoThread : public QThread
{
    Q_OBJECT
public:
    explicit VideoThread(QObject *parent = nullptr);
    ~VideoThread();

    void setUrl(const QString &url);
    void stop();

signals:
    void frameCaptured(const QImage &image);

protected:
    void run() override;

private:
    QString m_url;
    bool m_stop;
    QMutex m_mutex;
    cv::VideoCapture m_cap;
};

#endif // VIDEOTHREAD_H
