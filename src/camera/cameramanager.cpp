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

void CameraManager::startDisplayPipeline() {
  createThreads();

  if (!m_connectionInfo.isValid()) {
    emit logMessage("Error: camera connection info is not configured.");
    return;
  }

  const QString url =
      buildRtspUrl(m_connectionInfo.ip, m_connectionInfo.username,
                   m_connectionInfo.password, m_connectionInfo.profile);
  emit logMessage(
      QString("Starting camera[%1] display pipeline with IP=%2, profile=%3")
          .arg(m_connectionInfo.cameraId.isEmpty() ? QStringLiteral("camera-1")
                                                   : m_connectionInfo.cameraId,
               m_connectionInfo.ip, m_connectionInfo.profile));

  m_videoThread->setUrl(url);
  m_videoThread->start();

  m_metadataThread->setConnectionInfo(
      m_connectionInfo.ip, m_connectionInfo.username, m_connectionInfo.password,
      m_connectionInfo.profile);
  m_metadataThread->start();
}

void CameraManager::start() {
  if (isRunning()) {
    return;
  }

  createThreads();

  if (!m_connectionInfo.isValid()) {
    emit logMessage("Error: camera connection info is not configured.");
    return;
  }

  startDisplayPipeline();

  const QString ocrProfile = m_connectionInfo.subProfile.trimmed().isEmpty()
                                 ? m_connectionInfo.profile
                                 : m_connectionInfo.subProfile.trimmed();
  const QString ocrUrl =
      buildRtspUrl(m_connectionInfo.ip, m_connectionInfo.username,
                   m_connectionInfo.password, ocrProfile);

  if (m_ocrVideoThread) {
    m_ocrVideoThread->setUrl(ocrUrl);
    m_ocrVideoThread->setTargetFps(2);
    m_ocrVideoThread->start();
  }
}

void CameraManager::startVideoOnly() {
  if (isRunning()) {
    return;
  }

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

void CameraManager::restartDisplayPipeline() {
  if (!m_connectionInfo.isValid()) {
    emit logMessage("Error: camera connection info is not configured.");
    return;
  }

  createThreads();
  stopThread(m_videoThread, QStringLiteral("display video thread"), false);
  stopThread(m_metadataThread, QStringLiteral("metadata thread"), false);
  startDisplayPipeline();
}

void CameraManager::stopThread(QThread *thread, const QString &name,
                               bool warnOnFailure) {
  if (!thread || !thread->isRunning()) {
    return;
  }

  if (auto *videoThread = qobject_cast<VideoThread *>(thread)) {
    videoThread->stop();
  } else if (auto *metadataThread = qobject_cast<MetadataThread *>(thread)) {
    metadataThread->stop();
  } else {
    thread->quit();
  }

  if (!thread->wait(kThreadStopTimeoutMs)) {
    if (warnOnFailure) {
      emit logMessage(QString("Warning: %1 stop timeout (%2 ms). Forcing terminate.")
                          .arg(name)
                          .arg(kThreadStopTimeoutMs));
    }
    thread->terminate();
    if (!thread->wait(kForceStopWaitMs) && warnOnFailure) {
      emit logMessage(QString("Error: %1 did not terminate cleanly.").arg(name));
    }
  }
}

void CameraManager::stop() {
  QElapsedTimer shutdownTimer;
  shutdownTimer.start();

  stopThread(m_videoThread, QStringLiteral("display video thread"), false);
  stopThread(m_ocrVideoThread, QStringLiteral("OCR video thread"), true);
  stopThread(m_metadataThread, QStringLiteral("metadata thread"), false);

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
