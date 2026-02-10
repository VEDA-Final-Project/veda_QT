#ifndef CAMERASESSIONSERVICE_H
#define CAMERASESSIONSERVICE_H

#include "core/cameramanager.h"
#include "core/metadatasynchronizer.h"
#include <QList>

class CameraSessionService
{
public:
  void setCameraManager(CameraManager *cameraManager);
  void setDelayMs(int delayMs);

  void playOrRestart();
  void stop();

  void pushMetadata(const QList<ObjectInfo> &objects, qint64 tsMs);
  QList<ObjectInfo> consumeReadyMetadata(qint64 nowMs);

private:
  CameraManager *m_cameraManager = nullptr;
  MetadataSynchronizer m_metadataSynchronizer;
};

#endif // CAMERASESSIONSERVICE_H
