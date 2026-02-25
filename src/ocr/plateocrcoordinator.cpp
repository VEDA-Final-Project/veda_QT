#include "ocr/plateocrcoordinator.h"
#include "config/config.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

namespace {
constexpr int kVoteWindow = 5;
constexpr int kMinMajorityVotes = 3;
constexpr qint64 kLastGoodHoldMs = 2000;
constexpr qint64 kStateCacheTtlMs = 120000;
const QRegularExpression kFinalPlatePattern(
    QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));
} // namespace

PlateOcrCoordinator::PlateOcrCoordinator(QObject *parent) : QObject(parent)
{
  const auto &cfg = Config::instance();
  if (!m_ocrManager.init(cfg.tessdataPath(), cfg.ocrLanguage()))
  {
    qDebug() << "Could not initialize OCR Manager.";
  }

  connect(&m_watcher, &QFutureWatcher<OcrResult>::finished, this,
          &PlateOcrCoordinator::onOcrFinished);
}

PlateOcrCoordinator::~PlateOcrCoordinator()
{
  m_shuttingDown = true;
  m_pending.clear();
  m_inflightObjectIds.clear();
  m_objectStates.clear();
  if (m_watcher.isRunning())
  {
    m_watcher.cancel();
    m_watcher.waitForFinished();
  }
}

void PlateOcrCoordinator::requestOcr(int objectId, const QImage &crop)
{
  if (m_shuttingDown)
  {
    return;
  }

  if (objectId < 0 || crop.isNull())
  {
    return;
  }

  if (m_inflightObjectIds.contains(objectId))
  {
    return;
  }

  m_inflightObjectIds.insert(objectId);
  m_pending.append(PendingOcr{objectId, crop});
  startNext();
}

void PlateOcrCoordinator::onOcrFinished()
{
  if (m_shuttingDown)
  {
    return;
  }

  const OcrResult rawResult = m_watcher.result();
  const int objectId = m_runningObjectId;
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  m_inflightObjectIds.remove(objectId);
  m_runningObjectId = -1;

  QString debugReason;
  const QString stableResult =
      resolveStableResult(objectId, rawResult, nowMs, &debugReason);
  if (!stableResult.isEmpty())
  {
    emit ocrReady(objectId, stableResult);
  }
  else if (!debugReason.isEmpty())
  {
    qDebug() << "[OCR][Drop] ID:" << objectId << debugReason;
  }

  pruneStateCache(nowMs);
  startNext();
}

QString PlateOcrCoordinator::resolveStableResult(int objectId,
                                                 const OcrResult &rawResult,
                                                 qint64 nowMs,
                                                 QString *debugReason)
{
  if (debugReason)
  {
    debugReason->clear();
  }
  if (objectId < 0)
  {
    if (debugReason)
    {
      *debugReason = QStringLiteral("invalid object id");
    }
    return QString();
  }

  ObjectOcrState &state = m_objectStates[objectId];
  state.lastSeenMs = nowMs;

  if (!rawResult.selectedCandidate.isEmpty())
  {
    state.recentVotes.enqueue(rawResult.selectedCandidate);
    while (state.recentVotes.size() > kVoteWindow)
    {
      state.recentVotes.dequeue();
    }
  }

  int voteCount = 0;
  const QString voteWinner = majorityVote(state.recentVotes, &voteCount);
  const bool winnerPatternOk = kFinalPlatePattern.match(voteWinner).hasMatch();
  QString finalText;

  if (!voteWinner.isEmpty() && voteCount >= kMinMajorityVotes && winnerPatternOk)
  {
    finalText = voteWinner;
  }
  else if (!state.lastGood.isEmpty() && (nowMs - state.lastGoodMs) <= kLastGoodHoldMs)
  {
    finalText = state.lastGood;
    if (debugReason)
    {
      *debugReason = QString("majority undecided, use last-good '%1' (%2/%3)")
                         .arg(finalText)
                         .arg(voteCount)
                         .arg(kMinMajorityVotes);
    }
  }
  else
  {
    if (debugReason)
    {
      if (!voteWinner.isEmpty() && !winnerPatternOk)
      {
        *debugReason = QString("majority candidate '%1' failed final pattern")
                           .arg(voteWinner);
      }
      else if (!voteWinner.isEmpty())
      {
        *debugReason =
            QString("majority not reached yet (%1/%2), candidate='%3'")
                .arg(voteCount)
                .arg(kMinMajorityVotes)
                .arg(voteWinner);
      }
      else if (!rawResult.dropReason.isEmpty())
      {
        *debugReason = rawResult.dropReason;
      }
      else
      {
        *debugReason = QStringLiteral("undecided and no last-good");
      }
    }
    return QString();
  }

  state.lastGood = finalText;
  state.lastGoodMs = nowMs;

  if (state.lastEmitted == finalText)
  {
    return QString();
  }
  state.lastEmitted = finalText;
  return finalText;
}

QString PlateOcrCoordinator::majorityVote(const QQueue<QString> &votes,
                                          int *countOut)
{
  QHash<QString, int> histogram;
  QString bestText;
  int bestCount = 0;

  for (const QString &value : votes)
  {
    if (value.isEmpty())
    {
      continue;
    }
    const int count = ++histogram[value];
    if (count > bestCount)
    {
      bestCount = count;
      bestText = value;
    }
  }

  if (countOut)
  {
    *countOut = bestCount;
  }
  return bestText;
}

void PlateOcrCoordinator::pruneStateCache(qint64 nowMs)
{
  auto it = m_objectStates.begin();
  while (it != m_objectStates.end())
  {
    if ((nowMs - it.value().lastSeenMs) > kStateCacheTtlMs)
    {
      it = m_objectStates.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void PlateOcrCoordinator::startNext()
{
  if (m_shuttingDown)
  {
    return;
  }

  if (m_watcher.isRunning() || m_pending.isEmpty())
  {
    return;
  }

  const PendingOcr next = m_pending.dequeue();
  m_runningObjectId = next.objectId;

  QFuture<OcrResult> future = QtConcurrent::run(
      [this, crop = next.crop]()
      { return m_ocrManager.performOcrDetailed(crop); });
  m_watcher.setFuture(future);
}
