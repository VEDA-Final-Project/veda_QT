#ifndef LOGDEDUPLICATOR_H
#define LOGDEDUPLICATOR_H

#include <QString>
#include <QtGlobal>

class LogDeduplicator
{
public:
  explicit LogDeduplicator(qint64 duplicateWindowMs = 2000);

  struct IngestResult
  {
    bool suppressed = false;
    QString flushSummary;
  };

  IngestResult ingest(const QString &message, qint64 nowMs);
  QString flushPending();

private:
  qint64 m_duplicateWindowMs = 2000;
  QString m_lastMessage;
  qint64 m_lastMessageMs = 0;
  int m_suppressedCount = 0;
};

#endif // LOGDEDUPLICATOR_H
