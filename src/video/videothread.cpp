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

void VideoThread::setTargetFps(int fps) {
  QMutexLocker locker(&m_mutex);
  m_targetFps = fps;
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
   * === RTSP 최적화 설정 ===
   * - 내부 버퍼 최소화(0)
   */
  m_cap.set(cv::CAP_PROP_BUFFERSIZE, 0);

  // === RTSP 스트림 열기 ===
  if (!m_cap.open(url.toStdString(), cv::CAP_FFMPEG)) {
    QString safeUrl = url;
    const int atPos = safeUrl.indexOf('@');
    if (safeUrl.startsWith(QStringLiteral("rtsp://")) && atPos > 0) {
      safeUrl = QStringLiteral("rtsp://***:***") + safeUrl.mid(atPos);
    }
    emit logMessage(QString("Error: Cannot open stream: %1").arg(safeUrl));
    return;
  }

  const double sourceFps = m_cap.get(cv::CAP_PROP_FPS);
  qDebug() << "[Video] RTSP Source Native FPS for" << url << ":" << sourceFps;

  qint64 frameCount = 0;
  qint64 lastFpsTimeMs = QDateTime::currentMSecsSinceEpoch();

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
          emit logMessage(
              QString("Error: Cannot read frame (repeated %1 times)")
                  .arg(suppressedReadErrors));
          suppressedReadErrors = 0;
        } else {
          emit logMessage(QStringLiteral("Error: Cannot read frame"));
        }
        lastReadErrorLogMs = nowMs;
      } else {
        ++suppressedReadErrors;
      }
      // 일시적 네트워크 문제 대비
      QThread::msleep(100);
      continue;
    }

    // === 타겟 FPS에 맞춘 프레임 스킵 최적화 (CPU/Memory 낭비 방지) ===
    const qint64 readEndMs = QDateTime::currentMSecsSinceEpoch();
    int targetFps = 0;
    {
      QMutexLocker locker(&m_mutex);
      targetFps = m_targetFps;
    }

    if (targetFps > 0) {
      if (lastFpsTimeMs > 0) {
        qint64 elapsedSinceLastEmitted = readEndMs - lastFpsTimeMs;
        qint64 targetIntervalMs = 1000 / targetFps;
        if (elapsedSinceLastEmitted < targetIntervalMs) {
          // 아직 다음 프레임 보낼 타이밍이 아니면 스킵 (clone() 및 시그널 발생
          // 안 함)
          continue;
        }
      }
    }

    frameCount++;
    if (targetFps == 0 && (readEndMs - lastFpsTimeMs > 5000)) {
      double actualFps = (frameCount * 1000.0) / (readEndMs - lastFpsTimeMs);
      qDebug() << "[Video] Incoming Frame Loop FPS for" << url << ":"
               << actualFps;
      frameCount = 0;
      lastFpsTimeMs = readEndMs;
    }

    if (targetFps > 0) {
      lastFpsTimeMs = readEndMs;
    }

    // === 메모리 보호: 빈 프레임 무시 ===
    if (frame.empty()) {
      continue;
    }

    // === Zero-Copy 전송: clone()으로 독립 데이터 확보 ===
    // OpenCV의 frame 버퍼 재사용을 유지하면서, UI에 넘길 독립 복사본을
    // 생성합니다. BGR → RGB 색상 변환은 UI 스레드에서 렌더링할 프레임에만
    // 수행합니다. (불필요한 변환 방지)
    auto sharedFrame = QSharedPointer<cv::Mat>::create(frame.clone());

    // === 프레임 전달 (UI 등 외부로) ===
    emit frameCaptured(sharedFrame, QDateTime::currentMSecsSinceEpoch());
  }

  // === 스트림 정리 ===
  m_cap.release();
}
