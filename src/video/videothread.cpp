#include "videothread.h"
#include <QDateTime>
#include <QDebug>

/**
 * @brief VideoThread 생성자
 * - OpenCV 기반 RTSP 비디오 수신 전용 스레드
 */
VideoThread::VideoThread(QObject *parent)
    : QThread(parent),
    m_stop(false)
{
}

/**
 * @brief VideoThread 소멸자
 * - 실행 중인 스레드를 안전하게 종료
 */
VideoThread::~VideoThread()
{
    stop();   // 종료 플래그 설정
    wait();   // 스레드 종료 대기
}

/**
 * @brief RTSP 스트림 URL 설정
 * @param url RTSP 접속 URL
 *
 * 멀티스레드 환경에서 안전하게 접근하기 위해
 * 뮤텍스로 보호
 */
void VideoThread::setUrl(const QString &url)
{
    QMutexLocker locker(&m_mutex);
    m_url = url;
}

/**
 * @brief 비디오 스레드 종료 요청
 * - run() 루프에서 주기적으로 플래그 확인
 */
void VideoThread::stop()
{
    QMutexLocker locker(&m_mutex);
    m_stop = true;
}

/**
 * @brief 비디오 수신 메인 루프
 * - RTSP 스트림 연결
 * - 프레임 수신 및 QImage 변환
 */
void VideoThread::run()
{
    QString url;
    constexpr qint64 kReadErrorLogIntervalMs = 3000;
    qint64 lastReadErrorLogMs = 0;
    int suppressedReadErrors = 0;

    // === URL 복사 및 상태 초기화 ===
    {
        QMutexLocker locker(&m_mutex);
        url = m_url;
        m_stop = false;
    }

    /**
     * === RTSP 설정 ===
     * - TCP 전송 강제 (네트워크 안정성 우선)
     * - 내부 버퍼 최소화 → 지연(latency) 감소 목적
     *
     * OpenCV FFmpeg 백엔드 사용 시
     * 환경 변수나 open 옵션 추가 필요할 수 있음
     */
    m_cap.set(cv::CAP_PROP_BUFFERSIZE, 0);

    // === RTSP 스트림 열기 ===
    if (!m_cap.open(url.toStdString(), cv::CAP_FFMPEG)) {
        qDebug() << "Error: Cannot open stream" << url;
        return;
    }

    // === 프레임 수신 루프 ===
    while (true) {

        // === 종료 요청 확인 ===
        {
            QMutexLocker locker(&m_mutex);
            if (m_stop)
                break;
        }

        cv::Mat frame;

        // === 프레임 읽기 ===
        if (!m_cap.read(frame)) {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            if (lastReadErrorLogMs == 0 || (nowMs - lastReadErrorLogMs) >= kReadErrorLogIntervalMs) {
                if (suppressedReadErrors > 0) {
                    qDebug() << "Error: Cannot read frame (repeated" << suppressedReadErrors
                             << "times)";
                    suppressedReadErrors = 0;
                } else {
                    qDebug() << "Error: Cannot read frame";
                }
                lastReadErrorLogMs = nowMs;
            } else {
                ++suppressedReadErrors;
            }
            // 일시적 네트워크 문제 대비
            QThread::msleep(100);
            continue;
        }

        // === 유효하지 않은 프레임 방어 ===
        if (frame.empty())
            continue;

        // === OpenCV(BGR) → Qt(RGB) 변환 ===
        cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

        /**
         * === QImage 생성 ===
         * - OpenCV Mat 데이터는 루프 종료 시 해제될 수 있으므로
         *   copy()를 사용하여 깊은 복사 수행
         */
        QImage qimg(frame.data,
                    frame.cols,
                    frame.rows,
                    frame.step,
                    QImage::Format_RGB888);

        // === 프레임 전달 (UI 등 외부로) ===
        emit frameCaptured(qimg.copy());
    }

    // === 스트림 정리 ===
    m_cap.release();
}
