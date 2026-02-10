#include "metadatasynchronizer.h"
#include <QtGlobal>

void MetadataSynchronizer::setDelayMs(int delayMs)
{
  m_delayMs = qMax(0, delayMs);
}

void MetadataSynchronizer::pushMetadata(const QList<ObjectInfo> &objects, qint64 tsMs)
{
  m_queue.append(qMakePair(tsMs, objects));
}

QList<ObjectInfo> MetadataSynchronizer::consumeReady(qint64 nowMs)
{
  while (!m_queue.isEmpty())
  {
    if (m_queue.first().first + m_delayMs <= nowMs)
    {
      m_currentObjects = m_queue.takeFirst().second;
    }
    else
    {
      break;
    }
  }
  return m_currentObjects;
}
