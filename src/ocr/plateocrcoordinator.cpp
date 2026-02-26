#include "ocr/plateocrcoordinator.h"
#include "config/config.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

namespace {
constexpr int kVoteWindow = 5;
constexpr int kMinMajorityVotes = 3;
constexpr qint64 kLastGoodHoldMs = 5000;
constexpr qint64 kStateCacheTtlMs = 120000;
const QRegularExpression kFinalPlatePattern(
    QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));

bool isHangulSyllable(const QChar ch)
{
  const ushort u = ch.unicode();
  return (u >= 0xAC00 && u <= 0xD7A3);
}

QString canonicalizePlateCandidate(const QString &candidate)
{
  const QString text = candidate.trimmed();
  if (text.isEmpty())
  {
    return QString();
  }
  if (kFinalPlatePattern.match(text).hasMatch())
  {
    return text;
  }
  if (text.size() < 7 || text.size() > 12)
  {
    return QString();
  }

  int hangulIndex = -1;
  int hangulCount = 0;
  for (int i = 0; i < text.size(); ++i)
  {
    const QChar ch = text.at(i);
    if (isHangulSyllable(ch))
    {
      hangulIndex = i;
      ++hangulCount;
      continue;
    }
    if (!ch.isDigit())
    {
      return QString();
    }
  }

  if (hangulCount != 1 || hangulIndex <= 0 || hangulIndex >= (text.size() - 1))
  {
    return QString();
  }

  int left = hangulIndex - 1;
  while (left >= 0 && text.at(left).isDigit())
  {
    --left;
  }
  int right = hangulIndex + 1;
  while (right < text.size() && text.at(right).isDigit())
  {
    ++right;
  }

  const int beforeDigits = hangulIndex - (left + 1);
  const int afterDigits = right - (hangulIndex + 1);
  if (beforeDigits < 2 || afterDigits < 4)
  {
    return QString();
  }

  const QString hangul = text.mid(hangulIndex, 1);
  const QString tail = text.mid(hangulIndex + 1, 4);

  const int preferredHeadLen = (beforeDigits == 2) ? 2 : 3;
  const int fallbackHeadLen = (preferredHeadLen == 3) ? 2 : 3;

  auto buildPlate = [&](int headLen) -> QString {
    if (beforeDigits < headLen)
    {
      return QString();
    }
    const QString head = text.mid(hangulIndex - headLen, headLen);
    const QString plate = head + hangul + tail;
    return kFinalPlatePattern.match(plate).hasMatch() ? plate : QString();
  };

  const QString preferred = buildPlate(preferredHeadLen);
  if (!preferred.isEmpty())
  {
    return preferred;
  }
  return buildPlate(fallbackHeadLen);
}
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

  const QString voteCandidate = !rawResult.text.isEmpty()
                                    ? rawResult.text
                                    : canonicalizePlateCandidate(rawResult.selectedCandidate);
  if (!voteCandidate.isEmpty())
  {
    state.recentVotes.enqueue(voteCandidate);
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
