#ifndef VIDEOFRAMERENDERER_H
#define VIDEOFRAMERENDERER_H

#include "metadata/metadatathread.h"
#include <QImage>
#include <QList>
#include <QPolygon>
#include <QSet>
#include <QStringList>

struct OcrRequest {
  int objectId = -1;
  QImage crop;
};

class VideoFrameRenderer {
public:
  QImage compose(const QImage &frame, const QSize &targetSize,
                 const QList<ObjectInfo> &objects,
                 const QList<QPolygon> &roiPolygons,
                 const QStringList &roiLabels, bool roiEnabled,
                 const QSet<int> &occupiedRoiIndices,
                 QList<OcrRequest> *ocrRequests) const;
};

#endif // VIDEOFRAMERENDERER_H
