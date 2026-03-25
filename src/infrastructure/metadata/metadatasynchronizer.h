#ifndef METADATASYNCHRONIZER_H
#define METADATASYNCHRONIZER_H

#include "infrastructure/metadata/objectinfo.h"
#include <QList>
#include <QPair>
#include <QQueue>
#include <QtGlobal>

class MetadataSynchronizer
{
public:
  void setDelayMs(int delayMs);
  void pushMetadata(const QList<ObjectInfo> &objects, qint64 tsMs);
  QList<ObjectInfo> consumeReady(qint64 nowMs);

private:
  QQueue<QPair<qint64, QList<ObjectInfo>>> m_queue;
  QList<ObjectInfo> m_currentObjects;
  int m_delayMs = 0;
};

#endif // METADATASYNCHRONIZER_H
