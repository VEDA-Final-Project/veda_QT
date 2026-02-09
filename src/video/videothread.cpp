
#include "videothread.h"
#include <QDebug>

VideoThread::VideoThread(QObject *parent)
    : QThread(parent), m_stop(false)
{
}

VideoThread::~VideoThread()
{
    stop();
    wait();
}

void VideoThread::setUrl(const QString &url)
{
    QMutexLocker locker(&m_mutex);
    m_url = url;
}

void VideoThread::stop()
{
    QMutexLocker locker(&m_mutex);
    m_stop = true;
}

void VideoThread::run()
{
    QString url;
    {
        QMutexLocker locker(&m_mutex);
        url = m_url;
        m_stop = false;
    }

    // TCP 전송 모드 강제 설정 및 버퍼 최소화
    // FFMPEG 백엔드 사용 시 환경변수 또는 open() 옵션 필요할 수 있음

    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 0); // 버퍼 최소화 시도

    if (!m_cap.open(url.toStdString(), cv::CAP_FFMPEG)) {
        qDebug() << "Error: Cannot open stream" << url;
        return;
    }

    while (true) {
        {
            QMutexLocker locker(&m_mutex);
            // Check stop flag frequently for responsive shutdown.
            if (m_stop) break;
        }

        cv::Mat frame;
        if (!m_cap.read(frame)) {
            qDebug() << "Error: Cannot read frame";
            // 재접속 로직이 필요할 수 있으나 테스트용으론 루프 종료 혹은 잠시 대기
            QThread::msleep(100);
            continue;
        }

        if (frame.empty()) continue;

        // OpenCV(BGR) -> Qt(RGB) 변환
        cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

        // QImage 생성 (데이터 복사 발생)
        // 주의: frame.data는 루프 내에서 유효해야 하므로 copy()를 사용하여 깊은 복사를 권장
        QImage qimg(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

        emit frameCaptured(qimg.copy());
    }

    m_cap.release();
}



