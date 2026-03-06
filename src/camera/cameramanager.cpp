#include "cameramanager.h"
#include "util/rtspurl.h"
#include <QElapsedTimer>

namespace
{
  constexpr unsigned long kThreadStopTimeoutMs = 2000;

  template <typename T>
  void recycleFinishedThread(T *&thread)
  {
    if (!thread)
    {
      return;
    }
    if (thread->isRunning())
    {
      return;
    }
    if (!thread->isFinished())
    {
      return;
    }
    thread->deleteLater();
    thread = nullptr;
  }
} // namespace

/**
 * @brief CameraManager 생성자
 */
CameraManager::CameraManager(QObject *parent)
    : QObject(parent), m_videoThread(nullptr), m_ocrVideoThread(nullptr),
      m_metadataThread(nullptr)
{
  createThreads();
}

CameraManager::~CameraManager() { stop(); }

void CameraManager::createThreads()
{
  // stop() 타임아웃 이후 스레드가 뒤늦게 종료되었을 수 있으므로 재활용 처리
  recycleFinishedThread(m_videoThread);
  recycleFinishedThread(m_ocrVideoThread);
  recycleFinishedThread(m_metadataThread);

  if (!m_videoThread)
  {
    m_videoThread = new VideoThread(this);
    connect(m_videoThread, &VideoThread::frameCaptured, this,
            &CameraManager::frameCaptured);
    connect(m_videoThread, &VideoThread::logMessage, this,
            &CameraManager::logMessage);
  }

  if (!m_ocrVideoThread)
  {
    m_ocrVideoThread = new VideoThread(this);
    connect(m_ocrVideoThread, &VideoThread::frameCaptured, this,
            &CameraManager::ocrFrameCaptured);
    connect(m_ocrVideoThread, &VideoThread::logMessage, this,
            &CameraManager::logMessage);
  }

  if (!m_metadataThread)
  {
    m_metadataThread = new MetadataThread(this);
    connect(m_metadataThread, &MetadataThread::metadataReceived, this,
            &CameraManager::metadataReceived);
    connect(m_metadataThread, &MetadataThread::logMessage, this,
            &CameraManager::logMessage);
  }

  if (!isRunning())
  {
    m_stopTimedOut = false;
  }
}

void CameraManager::setConnectionInfo(
    const CameraConnectionInfo &connectionInfo)
{
  m_connectionInfo = connectionInfo;
}

void CameraManager::start()
{
  if (isRunning())
    return;

  createThreads();

  if (!m_connectionInfo.isValid())
  {
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
  if (m_ocrVideoThread)
  {
    m_ocrVideoThread->setUrl(ocrUrl);
    m_ocrVideoThread->setTargetFps(
        2); // 4K OCR은 초당 2프레임이면 충분함 (CPU 부하 대폭 감소)
    m_ocrVideoThread->start();
  }

  // === 메타데이터 스트림 시작 ===
  // 비디오와 동일한 profile 사용 → 싱크 유지
  m_metadataThread->setConnectionInfo(
      m_connectionInfo.ip, m_connectionInfo.username, m_connectionInfo.password,
      m_connectionInfo.profile);
  m_metadataThread->start();
}

void CameraManager::startVideoOnly()
{
  if (isRunning())
    return;

  createThreads();

  if (!m_connectionInfo.isValid())
  {
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

void CameraManager::setTargetFps(int fps)
{
  if (m_videoThread)
  {
    m_videoThread->setTargetFps(fps);
  }
}

void CameraManager::restart()
{
  this->stop();
  if (m_stopTimedOut || isRunning())
  {
    emit logMessage(
        QString("Warning: restart skipped due to incomplete thread stop."));
    return;
  }
  this->start();
}

void CameraManager::stop()
{
  QElapsedTimer shutdownTimer;
  shutdownTimer.start();
  m_stopTimedOut = false;

  // === 먼저 모든 스레드에 중지 신호를 브로드캐스트 ===
  if (m_videoThread && m_videoThread->isRunning())
  {
    m_videoThread->stop();
  }
  if (m_ocrVideoThread && m_ocrVideoThread->isRunning())
  {
    m_ocrVideoThread->stop();
  }
  if (m_metadataThread && m_metadataThread->isRunning())
  {
    m_metadataThread->stop();
  }

  auto waitWithTimeout = [this](QThread *thread,
                                const QString &threadName) -> bool
  {
    if (!thread || !thread->isRunning())
    {
      return true;
    }
    if (thread->wait(kThreadStopTimeoutMs))
    {
      return true;
    }
    m_stopTimedOut = true;
    emit logMessage(QString("Error: %1 stop timeout (%2 ms). Keeping thread "
                            "instance to avoid unsafe restart.")
                        .arg(threadName)
                        .arg(kThreadStopTimeoutMs));
    return false;
  };

  const bool videoStopped = waitWithTimeout(m_videoThread, "Video thread");
  const bool ocrVideoStopped =
      waitWithTimeout(m_ocrVideoThread, "OCR video thread");
  const bool metadataStopped =
      waitWithTimeout(m_metadataThread, "Metadata thread");

  if (m_videoThread && (!m_videoThread->isRunning()) && videoStopped)
  {
    m_videoThread->deleteLater();
    m_videoThread = nullptr;
  }

  if (m_ocrVideoThread && (!m_ocrVideoThread->isRunning()) && ocrVideoStopped)
  {
    m_ocrVideoThread->deleteLater();
    m_ocrVideoThread = nullptr;
  }

  if (m_metadataThread && (!m_metadataThread->isRunning()) && metadataStopped)
  {
    m_metadataThread->deleteLater();
    m_metadataThread = nullptr;
  }

  if (m_stopTimedOut)
  {
    emit logMessage(
        QString("Warning: one or more camera threads did not stop in time. "
                "Restart is blocked until cleanup completes."));
  }

  emit logMessage(
      QString("Camera stop completed in %1 ms.").arg(shutdownTimer.elapsed()));
}

bool CameraManager::isRunning() const
{
  bool videoRunning = m_videoThread && m_videoThread->isRunning();
  bool ocrVideoRunning = m_ocrVideoThread && m_ocrVideoThread->isRunning();
  bool metaRunning = m_metadataThread && m_metadataThread->isRunning();
  return videoRunning || ocrVideoRunning || metaRunning;
}
