#include "cameramanager.h"
#include "util/rtspurl.h"
#include <QElapsedTimer>

namespace {
constexpr unsigned long kThreadStopTimeoutMs = 2000;
constexpr unsigned long kForceStopWaitMs = 500;
} // namespace

/**
 * @brief CameraManager 생성자
 * - 비디오 스레드, 메타데이터 스레드 생성
 * - 내부 스레드의 시그널을 외부(CameraManager)로 전달
 */
CameraManager::CameraManager(QObject *parent) : QObject(parent) {

  // === 스레드 생성 (QObject 부모 설정으로 메모리 관리) ===
  m_videoThread = new VideoThread(this);
  m_metadataThread = new MetadataThread(this);

  // === VideoThread → CameraManager 시그널 전달 ===
  // 캡처된 프레임을 그대로 외부(UI 등)로 전달
  connect(m_videoThread, &VideoThread::frameCaptured, this,
          &CameraManager::frameCaptured);
  connect(m_videoThread, &VideoThread::logMessage, this,
          &CameraManager::logMessage);

  // === MetadataThread → CameraManager 시그널 전달 ===
  // 객체 메타데이터 전달
  connect(m_metadataThread, &MetadataThread::metadataReceived, this,
          &CameraManager::metadataReceived);

  // 로그 메시지 전달
  connect(m_metadataThread, &MetadataThread::logMessage, this,
          &CameraManager::logMessage);
}

/**
 * @brief CameraManager 소멸자
 * - 실행 중인 스레드가 있다면 안전하게 종료
 */
CameraManager::~CameraManager() { stop(); }

void CameraManager::setConnectionInfo(
    const CameraConnectionInfo &connectionInfo) {
  m_connectionInfo = connectionInfo;
}

/**
 * @brief 카메라 스트림 시작
 * - 주입된 연결 설정 사용
 * - RTSP 비디오 스트림 시작
 * - 메타데이터 스트림 시작
 */
void CameraManager::start() {

  // === 이미 실행 중이면 중복 실행 방지 ===
  if (isRunning())
    return;

  if (!m_connectionInfo.isValid()) {
    emit logMessage("Error: camera connection info is not configured.");
    return;
  }

  const QString url =
      buildRtspUrl(m_connectionInfo.ip, m_connectionInfo.username,
                   m_connectionInfo.password, m_connectionInfo.profile);

  // === 시작 로그 출력 ===
  emit logMessage(
      QString("Starting camera[%1] with IP=%2, profile=%3")
          .arg(m_connectionInfo.cameraId.isEmpty()
                   ? QStringLiteral("camera-1")
                   : m_connectionInfo.cameraId,
               m_connectionInfo.ip, m_connectionInfo.profile));
  emit logMessage(QString("RTSP URL: %1").arg(maskedRtspUrl(url)));

  // === 비디오 스트림 시작 ===
  m_videoThread->setUrl(url);
  m_videoThread->start();

  // === 메타데이터 스트림 시작 ===
  // 비디오와 동일한 profile 사용 → 싱크 유지
  m_metadataThread->setConnectionInfo(
      m_connectionInfo.ip, m_connectionInfo.username, m_connectionInfo.password,
      m_connectionInfo.profile);
  m_metadataThread->start();
}

/**
 * @brief 카메라 재시작
 * - 스트림 완전 종료 후 다시 시작
 */
void CameraManager::restart() {
  stop();
  start();
}

/**
 * @brief 카메라 스트림 종료
 * - 비디오/메타데이터 스레드를 안전하게 종료
 */
void CameraManager::stop() {
  QElapsedTimer shutdownTimer;
  shutdownTimer.start();

  // === 비디오 스레드 종료 ===
  if (m_videoThread->isRunning()) {
    m_videoThread->stop(); // 종료 요청
    if (!m_videoThread->wait(kThreadStopTimeoutMs)) {
      emit logMessage(
          QString(
              "Warning: video thread stop timeout (%1 ms). Forcing terminate.")
              .arg(kThreadStopTimeoutMs));
      m_videoThread->terminate();
      if (!m_videoThread->wait(kForceStopWaitMs)) {
        emit logMessage("Error: video thread did not terminate cleanly.");
      }
    }
  }

  // === 메타데이터 스레드 종료 ===
  if (m_metadataThread->isRunning()) {
    m_metadataThread->stop();
    if (!m_metadataThread->wait(kThreadStopTimeoutMs)) {
      emit logMessage(QString("Warning: metadata thread stop timeout (%1 ms). "
                              "Forcing terminate.")
                          .arg(kThreadStopTimeoutMs));
      m_metadataThread->terminate();
      if (!m_metadataThread->wait(kForceStopWaitMs)) {
        emit logMessage("Error: metadata thread did not terminate cleanly.");
      }
    }
  }

  emit logMessage(
      QString("Camera stop completed in %1 ms.").arg(shutdownTimer.elapsed()));
}

/**
 * @brief 카메라 실행 상태 확인
 * @return 하나라도 실행 중이면 true
 */
bool CameraManager::isRunning() const {
  return m_videoThread->isRunning() || m_metadataThread->isRunning();
}
