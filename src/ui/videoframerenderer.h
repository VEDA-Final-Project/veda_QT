#ifndef VIDEOFRAMERENDERER_H
#define VIDEOFRAMERENDERER_H

#include "metadatathread.h"
#include <QImage>
#include <QList>
#include <QPolygon>

struct OcrRequest
{
  int objectId = -1;
  QImage crop;
};

class VideoFrameRenderer
{
public:
  QImage compose(const QImage &frame, const QList<ObjectInfo> &objects,
                 const QList<QPolygon> &roiPolygons, bool roiEnabled,
                 QList<OcrRequest> *ocrRequests) const;
};

#endif // VIDEOFRAMERENDERER_H
