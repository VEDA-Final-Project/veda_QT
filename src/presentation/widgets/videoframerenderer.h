#ifndef VIDEOFRAMERENDERER_H
#define VIDEOFRAMERENDERER_H

#include "infrastructure/metadata/objectinfo.h"
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
  
  QImage compose(const QImage &sourceFrame, const QImage &scaledBaseFrame,
                 const QList<ObjectInfo> &objects,
                 const QList<QPolygon> &roiPolygons,
                 const QStringList &roiLabels,
                 const QSet<int> &occupiedRoiIndices, bool roiEnabled,
                 bool showFps, int currentFps, const QString &profileName,
                 double zoom = 1.0, double panX = 0.5, double panY = 0.5) const;
};

#endif // VIDEOFRAMERENDERER_H
