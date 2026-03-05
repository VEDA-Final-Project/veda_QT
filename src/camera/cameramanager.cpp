#include "cameramanager.h"
#include "util/rtspurl.h"
#include <QElapsedTimer>

namespace {
constexpr unsigned long kThreadStopTimeoutMs = 2000;
constexpr unsigned long kForceStopWaitMs = 500;
} // namespace

/**
 * @brief CameraManager 생성자
 */
CameraManager::CameraManager(QObject *parent)
    : QObject(parent), m_videoThread(nullptr), m_ocrVideoThread(nullptr),
      m_metadataThread(nullptr) {
  createThreads();
}

CameraManager::~CameraManager() { stop(); }

void CameraManager::createThreads() {
  if (!m_videoThread) {
    m_videoThread = new VideoThread(this);
    connect(m_videoThread, &VideoThread::frameCaptured, this,
            &CameraManager::frameCaptured);
    connect(m_videoThread, &VideoThread::logMessage, this,
            &CameraManager::logMessage);
  }

  if (!m_ocrVideoThread) {
    m_ocrVideoThread = new VideoThread(this);
    connect(m_ocrVideoThread, &VideoThread::frameCaptured, this,
            &CameraManager::ocrFrameCaptured);
    connect(m_ocrVideoThread, &VideoThread::logMessage, this,
            &CameraManager::logMessage);
  }

  if (!m_metadataThread) {
    m_metadataThread = new MetadataThread(this);
    connect(m_metadataThread, &MetadataThread::metadataReceived, this,
            &CameraManager::metadataReceived);
    connect(m_metadataThread, &MetadataThread::logMessage, this,
            &CameraManager::logMessage);
  }
}

void CameraManager::setConnectionInfo(
    const CameraConnectionInfo &connectionInfo) {
  m_connectionInfo = connectionInfo;
}

void CameraManager::start() {
  if (isRunning())
    return;

  createThreads();

  if (!m_connectionInfo.isValid()) {
    emit logMessage("Error: camera connection info is not configured.");
    return;
  }

  const QString url =
      buildRtspUrl(m_connectionInfo.ip, m_connectionInfo.username,
                   m_connectionInfo.password, m_connectionInfo.profile);
  const QString ocrProfile = m_connectionInfo.subProfile.trimmed().isEmpty()
                                 ? m_connectionInfo.profile
                                 : m_connectionInfo.subProfile.trimmed();
  const QString ocrUrl =
      buildRtspUrl(m_connectionInfo.ip, m_connectionInfo.username,
                   m_connectionInfo.password, ocrProfile);

  emit logMessage(QString("Starting camera[%1] with IP=%2, profile=%3")
                      .arg(m_connectionInfo.cameraId.isEmpty()
                               ? QStringLiteral("camera-1")
                               : m_connectionInfo.cameraId,
                           m_connectionInfo.ip, m_connectionInfo.profile));

  m_videoThread->setUrl(url);
  m_videoThread->start();

  // === OCR 전용 비디오 스트림 시작 ===
  if (m_ocrVideoThread) {
    m_ocrVideoThread->setUrl(ocrUrl);
    m_ocrVideoThread->start();
  }

  // === 메타데이터 스트림 시작 ===
  // 비디오와 동일한 profile 사용 → 싱크 유지
  m_metadataThread->setConnectionInfo(
      m_connectionInfo.ip, m_connectionInfo.username, m_connectionInfo.password,
      m_connectionInfo.profile);
  m_metadataThread->start();
}

void CameraManager::startVideoOnly() {
  if (isRunning())
    return;

  createThreads();

  if (!m_connectionInfo.isValid()) {
    emit logMessage("Error: camera connection info is not configured.");
    return;
  }

  const QString url =
      buildRtspUrl(m_connectionInfo.ip, m_connectionInfo.username,
                   m_connectionInfo.password, m_connectionInfo.profile);

  emit logMessage(
      QString("Starting camera[%1] video-only with IP=%2, profile=%3")
          .arg(m_connectionInfo.cameraId.isEmpty() ? QStringLiteral("camera-1")
                                                   : m_connectionInfo.cameraId,
               m_connectionInfo.ip, m_connectionInfo.profile));

  m_videoThread->setUrl(url);
  m_videoThread->start();
}

void CameraManager::setTargetFps(int fps) {
  if (m_videoThread) {
    m_videoThread->setTargetFps(fps);
  }
}

void CameraManager::restart() {
  this->stop();
  this->start();
}

void CameraManager::stop() {
  QElapsedTimer shutdownTimer;
  shutdownTimer.start();

  if (m_videoThread && m_videoThread->isRunning()) {
    m_videoThread->stop();
    if (!m_videoThread->wait(kThreadStopTimeoutMs)) {
      m_videoThread->terminate();
      m_videoThread->wait(kForceStopWaitMs);
    }
  }

  if (m_ocrVideoThread && m_ocrVideoThread->isRunning()) {
    m_ocrVideoThread->stop();
    if (!m_ocrVideoThread->wait(kThreadStopTimeoutMs)) {
      emit logMessage(
          QString("Warning: OCR video thread stop timeout (%1 ms). Forcing "
                  "terminate.")
              .arg(kThreadStopTimeoutMs));
      m_ocrVideoThread->terminate();
      if (!m_ocrVideoThread->wait(kForceStopWaitMs)) {
        emit logMessage("Error: OCR video thread did not terminate cleanly.");
      }
    }
  }

  // === 메타데이터 스레드 종료 ===
  if (m_metadataThread && m_metadataThread->isRunning()) {
    m_metadataThread->stop();
    if (!m_metadataThread->wait(kThreadStopTimeoutMs)) {
      m_metadataThread->terminate();
      m_metadataThread->wait(kForceStopWaitMs);
    }
  }

  if (m_videoThread) {
    m_videoThread->deleteLater();
    m_videoThread = nullptr;
  }

  if (m_ocrVideoThread) {
    m_ocrVideoThread->deleteLater();
    m_ocrVideoThread = nullptr;
  }

  if (m_metadataThread) {
    m_metadataThread->deleteLater();
    m_metadataThread = nullptr;
  }

  emit logMessage(
      QString("Camera stop completed in %1 ms.").arg(shutdownTimer.elapsed()));
}

bool CameraManager::isRunning() const {
  bool videoRunning = m_videoThread && m_videoThread->isRunning();
  bool ocrVideoRunning = m_ocrVideoThread && m_ocrVideoThread->isRunning();
  bool metaRunning = m_metadataThread && m_metadataThread->isRunning();
  return videoRunning || ocrVideoRunning || metaRunning;
}
