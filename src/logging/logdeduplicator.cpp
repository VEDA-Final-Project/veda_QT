#include "logging/logdeduplicator.h"

LogDeduplicator::LogDeduplicator(qint64 duplicateWindowMs)
    : m_duplicateWindowMs(duplicateWindowMs)
{
}

LogDeduplicator::IngestResult LogDeduplicator::ingest(const QString &message,
                                                      qint64 nowMs)
{
  IngestResult result;
  const bool isRapidDuplicate =
      (message == m_lastMessage) && (m_lastMessageMs > 0) &&
      ((nowMs - m_lastMessageMs) < m_duplicateWindowMs);

  if (isRapidDuplicate)
  {
    ++m_suppressedCount;
    m_lastMessageMs = nowMs;
    result.suppressed = true;
    return result;
  }

  result.flushSummary = flushPending();
  m_lastMessage = message;
  m_lastMessageMs = nowMs;
  return result;
}

QString LogDeduplicator::flushPending()
{
  if (m_suppressedCount <= 0 || m_lastMessage.isEmpty())
  {
    return QString();
  }

  const QString summary =
      QString("[Camera] previous log repeated %1 times: %2")
          .arg(m_suppressedCount)
          .arg(m_lastMessage);
  m_suppressedCount = 0;
  return summary;
}
