#ifndef CAMERASESSIONSERVICE_H
#define CAMERASESSIONSERVICE_H

#include "infrastructure/camera/cameramanager.h"
#include "infrastructure/metadata/metadatasynchronizer.h"
#include <QList>

class CameraSessionService
{
public:
  void setCameraManager(CameraManager *cameraManager);
  void setDelayMs(int delayMs);

  void playOrRestart();
  void stop();

  void pushMetadata(const QList<ObjectInfo> &objects, qint64 tsMs);
  QList<ObjectInfo> consumeReadyMetadata(qint64 frameTimestampMs);

private:
  CameraManager *m_cameraManager = nullptr;
  MetadataSynchronizer m_metadataSynchronizer;
};

#endif // CAMERASESSIONSERVICE_H
