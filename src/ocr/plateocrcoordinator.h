#ifndef PLATEOCRCOORDINATOR_H
#define PLATEOCRCOORDINATOR_H

#include "ocr/ocrmanager.h"
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QtConcurrent/QtConcurrent>

class PlateOcrCoordinator : public QObject
{
  Q_OBJECT

public:
  explicit PlateOcrCoordinator(QObject *parent = nullptr);
  ~PlateOcrCoordinator() override;

  void requestOcr(int objectId, const QImage &crop);

signals:
  void ocrReady(int objectId, const QString &text);

private slots:
  void onOcrFinished();

private:
  struct PendingOcr
  {
    int objectId = -1;
    QImage crop;
  };

  struct ObjectOcrState
  {
    QQueue<QString> recentVotes;
    QString lastGood;
    qint64 lastGoodMs = 0;
    QString lastEmitted;
    qint64 lastSeenMs = 0;
  };

  QString resolveStableResult(int objectId, const OcrResult &rawResult,
                              qint64 nowMs, QString *debugReason);
  static QString majorityVote(const QQueue<QString> &votes, int *countOut);
  void pruneStateCache(qint64 nowMs);
  void startNext();

  OcrManager m_ocrManager;
  QFutureWatcher<OcrResult> m_watcher;
  QQueue<PendingOcr> m_pending;
  QSet<int> m_inflightObjectIds;
  QHash<int, ObjectOcrState> m_objectStates;
  int m_runningObjectId = -1;
  bool m_shuttingDown = false;
};

#endif // PLATEOCRCOORDINATOR_H
