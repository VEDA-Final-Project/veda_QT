#include "videothread.h"
#include <QDateTime>
#include <QDebug>
#include <QTcpSocket>
#include <QUrl>
#include <QtGlobal>
#include <opencv2/core/ocl.hpp>


VideoThread::VideoThread(QObject *parent) : QThread(parent), m_stop(false) {
  qRegisterMetaType<SharedVideoFrame>("SharedVideoFrame");
}

VideoThread::~VideoThread() {
  stop();
  wait(); 
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

void VideoThread::stop() {
  QMutexLocker locker(&m_mutex);
  m_stop = true;
  requestInterruption();
}

double VideoThread::getActualFps() const {
  QMutexLocker locker(&m_mutex);
  return m_actualFps;
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
  auto shouldStop = [this]() {
    if (isInterruptionRequested()) {
      return true;
    }
    QMutexLocker locker(&m_mutex);
    return m_stop;
  };

  // === URL 복사 및 상태 초기화 ===
  {
    QMutexLocker locker(&m_mutex);
    url = m_url;
    m_stop = false;
  }

  // === 타임아웃 방지를 위한 TCP 사전 체크 ===
  // OpenCV FFmpeg `open`은 전역 락을 사용하므로, IP가 오프라인일 때 OS 기본 타임아웃(21초)
  // 동안 다른 채널의 연결까지 모두 블로킹하는 치명적인 데드락을 유발합니다.
  // 따라서 카메라가 응답 가능한지 TCP 접속 테스트를 먼저 수행합니다 (2초 타임아웃).
  {
    QUrl qurl(url);
    QString host = qurl.host();
    int port = qurl.port(554);

    // QUrl이 비밀번호 내 특수문자(@ 등) 때문에 파싱에 실패할 경우 수동 추출
    if (host.isEmpty()) {
      int atIndex = url.lastIndexOf('@');
      int slashIndex = url.indexOf('/', atIndex >= 0 ? atIndex : 0);
      if (slashIndex == -1) slashIndex = url.length();
      
      QString hostPort = url.mid(atIndex + 1, slashIndex - atIndex - 1);
      int colonIndex = hostPort.indexOf(':');
      if (colonIndex >= 0) {
        host = hostPort.mid(0, colonIndex);
        port = hostPort.mid(colonIndex + 1).toInt();
      } else {
        host = hostPort;
      }
    }

    if (!host.isEmpty()) {
      QTcpSocket socket;
      socket.connectToHost(host, port);
      if (!socket.waitForConnected(2000)) {
        emit logMessage(QString("Error: Camera %1:%2 is offline (TCP pre-check failed).").arg(host).arg(port));
        return; // 오프라인이면 FFmpeg open을 아예 호출하지 않음
      }
      socket.disconnectFromHost();
    }
  }

  /**
   * === RTSP 최적화 설정 ===
   * OpenCV FFmpeg 백엔드 타임아웃을 환경 변수로 전달 (단위: 마이크로초)
   * stimeout: TCP 기반 RTSP 연결 및 읽기 타임아웃 2초 (2,000,000)
   */
  // FFmpeg 캡처 옵션 설정: D3D11VA 하드웨어 디코딩 활성화 + RTSP TCP 전송 + 타임아웃 2초
  qputenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
          "hwaccel;d3d11va|rtsp_transport;tcp|stimeout;2000000");

  // === 하드웨어 가속을 스트림 열기 전에 설정 (중요!) ===
  m_cap.set(cv::CAP_PROP_HW_ACCELERATION, cv::VIDEO_ACCELERATION_D3D11);
  m_cap.set(cv::CAP_PROP_HW_DEVICE, 0);  // 첫 번째 GPU 디바이스

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

  // 하드웨어 가속 상태 확인
  int hw_accel = static_cast<int>(m_cap.get(cv::CAP_PROP_HW_ACCELERATION));
  emit logMessage(QString("[Video] Hardware Acceleration for %1: mode=%2 (0=None, 1=Any, 2=D3D11, 3=VAAPI, 4=MFX)")
                      .arg(url)
                      .arg(hw_accel));

  // 내부 버퍼 최소화 (지연 시간 감소를 위해 1로 설정)
  m_cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

  const double sourceFps = m_cap.get(cv::CAP_PROP_FPS);
  
  // OpenCL 가속 상태 확인 로그 추가
  bool has_ocl = cv::ocl::haveOpenCL();
  bool use_ocl = cv::ocl::useOpenCL();
  emit logMessage(QString("[Video] OpenCL status: Available=%1, Active=%2")
                  .arg(has_ocl ? "Yes" : "No")
                  .arg(use_ocl ? "Yes" : "No"));
  {
    QMutexLocker locker(&m_mutex);
    if (sourceFps > 0)
      m_actualFps = sourceFps;
  }
  qDebug() << "[Video] RTSP Source Native FPS for" << url << ":" << sourceFps;

  qint64 frameCount = 0;
  qint64 lastFpsTimeMs = QDateTime::currentMSecsSinceEpoch();

  // === 프레임 수신 루프 ===
  while (true) {

    // === 종료 요청 확인 ===
    if (shouldStop())
      break;

    cv::Mat frame;

    // === 프레임 읽기 ===
    if (!m_cap.read(frame)) {
      if (shouldStop())
        break;
      const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
      if (lastReadErrorLogMs == 0 ||(nowMs - lastReadErrorLogMs) >= kReadErrorLogIntervalMs) {
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
      if (shouldStop())
        break;
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

    // 내부 파이프라인은 BGR 원본을 유지하고, UI/OCR 직전에만 QImage 변환한다.
    SharedVideoFrame sharedFrame;
    sharedFrame.mat = QSharedPointer<cv::Mat>::create(std::move(frame));
    sharedFrame.timestampMs = QDateTime::currentMSecsSinceEpoch();

    // === 프레임 전달 (UI 등 외부로) ===
    emit frameCaptured(sharedFrame);
  }

  // === 스트림 정리 ===
  m_cap.release();
}
