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
  void collectOcrRequests(const QImage &frame, const QList<ObjectInfo> &objects,
                          QList<OcrRequest> *ocrRequests) const;
  QImage compose(const QImage &frame, const QSize &targetSize,
                 const QList<ObjectInfo> &objects,
                 const QList<QPolygon> &roiPolygons,
                 const QStringList &roiLabels,
                 const QSet<int> &occupiedRoiIndices, bool roiEnabled,
                 bool showFps, int currentFps, const QString &profileName,
                 QList<OcrRequest> *ocrRequests) const;
};

#endif // VIDEOFRAMERENDERER_H
