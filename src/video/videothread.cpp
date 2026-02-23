#include "videothread.h"
#include <QDateTime>
#include <QDebug>

/**
 * @brief VideoThread 생성자
 * - OpenCV 기반 RTSP 비디오 수신 전용 스레드
 */
VideoThread::VideoThread(QObject *parent) : QThread(parent), m_stop(false) {}

/**
 * @brief VideoThread 소멸자
 * - 실행 중인 스레드를 안전하게 종료
 */
VideoThread::~VideoThread() {
  stop(); // 종료 플래그 설정
  wait(); // 스레드 종료 대기
}

/**
 * @brief RTSP 스트림 URL 설정
 * @param url RTSP 접속 URL
 *
 * 멀티스레드 환경에서 안전하게 접근하기 위해
 * 뮤텍스로 보호
 */
void VideoThread::setUrl(const QString &url) {
  QMutexLocker locker(&m_mutex);
  m_url = url;
}

/**
 * @brief 비디오 스레드 종료 요청
 * - run() 루프에서 주기적으로 플래그 확인
 */
void VideoThread::stop() {
  QMutexLocker locker(&m_mutex);
  m_stop = true;
}

/**
 * @brief 비디오 수신 메인 루프
 * - RTSP 스트림 연결
 * - 프레임 수신 및 QImage 변환
 */
void VideoThread::run() {
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
   * - 실시간성 확보를 위해 최신 프레임을 유지하는 UDP 방식 우선
   * - 내부 버퍼 최소화(0)
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
      if (lastReadErrorLogMs == 0 ||
          (nowMs - lastReadErrorLogMs) >= kReadErrorLogIntervalMs) {
        if (suppressedReadErrors > 0) {
          qDebug() << "Error: Cannot read frame (repeated"
                   << suppressedReadErrors << "times)";
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

    // === QSharedPointer 기반 Zero-Copy 전송 준비 ===
    // OpenCV 버퍼 재사용 문제를 피하기 위해 매번 힙에 할당하여 소유권을
    // 전달합니다.
    QSharedPointer<cv::Mat> sharedFrame(new cv::Mat());
    frame.copyTo(*sharedFrame); // OpenCV 내부에서는 clone/copyTo를 통해 안전한
                                // 데이터 복제 수행

    // === 프레임 전달 (UI 등 외부로) ===
    emit frameCaptured(sharedFrame, QDateTime::currentMSecsSinceEpoch());
  }

  // === 스트림 정리 ===
  m_cap.release();
}
