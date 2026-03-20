#include "camera/camerasessionservice.h"

void CameraSessionService::setCameraManager(CameraManager *cameraManager)
{
  m_cameraManager = cameraManager;
}

void CameraSessionService::setDelayMs(int delayMs)
{
  m_metadataSynchronizer.setDelayMs(delayMs);
}

void CameraSessionService::playOrRestart()
{
  if (!m_cameraManager)
  {
    return;
  }
  if (m_cameraManager->isRunning())
  {
    m_cameraManager->restart();
    return;
  }
  m_cameraManager->start();
}

void CameraSessionService::stop()
{
  if (!m_cameraManager)
  {
    return;
  }
  m_cameraManager->stop();
}

void CameraSessionService::pushMetadata(const QList<ObjectInfo> &objects, qint64 tsMs)
{
  m_metadataSynchronizer.pushMetadata(objects, tsMs);
}

QList<ObjectInfo> CameraSessionService::consumeReadyMetadata(qint64 nowMs)
{
  return m_metadataSynchronizer.consumeReady(nowMs);
}
