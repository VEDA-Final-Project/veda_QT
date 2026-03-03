#include "ocr/plateocrcoordinator.h"
#include "config/config.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

namespace
{
constexpr int kStabilizationWindow = 5;
constexpr int kMinimumStableCount = 2;
constexpr qint64 kHistoryTtlMs = 15000;
constexpr int kRuntimeLogInterval = 10;

const QRegularExpression kPlatePattern(
    QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));

bool isDigitOnlyPlateCandidate(const QString &text)
{
    if (text.isEmpty())
    {
        return false;
    }

    for (const QChar ch : text)
    {
        if (!ch.isDigit())
        {
            return false;
        }
    }

    return text.size() == 7 || text.size() == 8;
}

bool extractPlateFormat(const QString &text, int *hangulIndexOut, QChar *hangulOut)
{
    if (!hangulIndexOut || !hangulOut || !kPlatePattern.match(text).hasMatch())
    {
        return false;
    }

    for (int i = 0; i < text.size(); ++i)
    {
        if (text.at(i).isDigit())
        {
            continue;
        }

        *hangulIndexOut = i;
        *hangulOut = text.at(i);
        return true;
    }

    return false;
}

QString restoreHangulFromTemplate(const QString &digitOnly,
                                  const QString &templatePlate)
{
    int hangulIndex = -1;
    QChar hangul;
    if (!extractPlateFormat(templatePlate, &hangulIndex, &hangul))
    {
        return QString();
    }

    QString withoutHangul = templatePlate;
    withoutHangul.remove(hangulIndex, 1);
    if (digitOnly == withoutHangul)
    {
        QString restored = digitOnly;
        restored.insert(hangulIndex, hangul);
        return restored;
    }

    if (digitOnly.size() != templatePlate.size() || hangulIndex >= digitOnly.size())
    {
        return QString();
    }

    if (digitOnly.left(hangulIndex) != templatePlate.left(hangulIndex))
    {
        return QString();
    }

    if (digitOnly.mid(hangulIndex + 1) != templatePlate.mid(hangulIndex + 1))
    {
        return QString();
    }

    QString restored = digitOnly;
    restored[hangulIndex] = hangul;
    return restored;
}

}

PlateOcrCoordinator::PlateOcrCoordinator(QObject *parent) : QObject(parent)
{
    const auto &cfg = Config::instance();
    for (size_t i = 0; i < m_workers.size(); ++i)
    {
        WorkerState &worker = m_workers[i];
        if (!worker.ocrManager.init(cfg.ocrModelPath(), cfg.ocrDictPath(),
                                    cfg.ocrInputWidth(), cfg.ocrInputHeight()))
        {
            qDebug() << "Could not initialize OCR Manager for worker" << i;
        }

        connect(&worker.watcher, &QFutureWatcher<QString>::finished, this,
                [this, i]() { onWorkerFinished(i); });
    }
}

PlateOcrCoordinator::~PlateOcrCoordinator()
{
    m_shuttingDown = true;
    m_pending.clear();
    m_inflightObjectIds.clear();
    m_pendingObjectIds.clear();
    m_histories.clear();
    for (WorkerState &worker : m_workers)
    {
        if (worker.watcher.isRunning())
        {
            worker.watcher.cancel();
            worker.watcher.waitForFinished();
        }
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

    if (m_inflightObjectIds.contains(objectId) || m_pendingObjectIds.contains(objectId))
    {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_pending.enqueue(PendingOcr{objectId, crop, nowMs});
    m_pendingObjectIds.insert(objectId);
    qDebug() << "[OCR][Enter] objectId=" << objectId
             << "pending=" << m_pending.size()
             << "inflight=" << m_inflightObjectIds.size();
    startNext();
}

void PlateOcrCoordinator::onWorkerFinished(const size_t workerIndex)
{
    if (m_shuttingDown)
    {
        return;
    }

    if (workerIndex >= m_workers.size())
    {
        return;
    }

    WorkerState &worker = m_workers[workerIndex];
    const QString result = worker.watcher.result();
    const int objectId = worker.runningObjectId;
    const qint64 finishedAtMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 processMs =
        (worker.startedAtMs > 0 && finishedAtMs > worker.startedAtMs)
            ? (finishedAtMs - worker.startedAtMs)
            : 0;
    const qint64 endToEndMs =
        (worker.queuedAtMs > 0 && finishedAtMs > worker.queuedAtMs)
            ? (finishedAtMs - worker.queuedAtMs)
            : processMs;
    const double speedHz =
        (processMs > 0) ? (1000.0 / static_cast<double>(processMs)) : 0.0;

    qDebug() << "[OCR][Done] objectId=" << objectId
             << "empty=" << result.isEmpty()
             << "processMs=" << processMs
             << "endToEndMs=" << endToEndMs
             << "speedHz=" << QString::number(speedHz, 'f', 2);

    m_inflightObjectIds.remove(objectId);
    m_pendingObjectIds.remove(objectId);
    worker.runningObjectId = -1;
    worker.queuedAtMs = 0;
    worker.startedAtMs = 0;

    if (!result.isEmpty())
    {
        const QString stabilized = stabilizeResult(objectId, result);
        if (!stabilized.isEmpty())
        {
            emit ocrReady(objectId, stabilized);
        }
    }

    startNext();
}

QString PlateOcrCoordinator::stabilizeResult(int objectId, const QString &result)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    pruneHistory(nowMs);

    OcrHistory &history = m_histories[objectId];
    const bool shouldLogThisFrame =
        ((++history.logFrameCount % kRuntimeLogInterval) == 0);
    const QString stabilizedInput = restoreDigitOnlyResult(history, result);
    if (shouldLogThisFrame && stabilizedInput != result)
    {
        qDebug() << "[OCR] Digit-only result restored from history. objectId="
                 << objectId << "raw=" << result << "restored=" << stabilizedInput;
    }

    if (!kPlatePattern.match(stabilizedInput).hasMatch())
    {
        if (shouldLogThisFrame)
        {
            qDebug() << "[OCR] Skipping non-matching OCR result. objectId="
                     << objectId << "raw=" << result << "candidate="
                     << stabilizedInput;
        }
        history.lastUpdatedMs = nowMs;
        return QString();
    }

    history.lastUpdatedMs = nowMs;
    history.recentResults.enqueue(stabilizedInput);
    while (history.recentResults.size() > kStabilizationWindow)
    {
        history.recentResults.dequeue();
    }

    QHash<QString, int> counts;
    QHash<QString, int> lastIndexByText;
    for (int i = 0; i < history.recentResults.size(); ++i)
    {
        const QString &text = history.recentResults[i];
        counts[text] += 1;
        lastIndexByText[text] = i;
    }

    QString bestText;
    int bestCount = -1;
    int bestLastIndex = -1;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
    {
        const QString &text = it.key();
        const int count = it.value();
        const int lastIndex = lastIndexByText.value(text, -1);
        if (count > bestCount ||
            (count == bestCount && lastIndex > bestLastIndex))
        {
            bestText = text;
            bestCount = count;
            bestLastIndex = lastIndex;
        }
    }

    if (bestText.isEmpty())
    {
        return QString();
    }

    if (bestCount < kMinimumStableCount)
    {
        return QString();
    }

    if (history.lastEmitted == bestText)
    {
        return QString();
    }

    history.lastEmitted = bestText;
    return bestText;
}

QString PlateOcrCoordinator::restoreDigitOnlyResult(const OcrHistory &history,
                                                    const QString &result) const
{
    if (!isDigitOnlyPlateCandidate(result))
    {
        return result;
    }

    for (int i = history.recentResults.size() - 1; i >= 0; --i)
    {
        const QString restored =
            restoreHangulFromTemplate(result, history.recentResults.at(i));
        if (!restored.isEmpty())
        {
            return restored;
        }
    }

    if (!history.lastEmitted.isEmpty())
    {
        const QString restored =
            restoreHangulFromTemplate(result, history.lastEmitted);
        if (!restored.isEmpty())
        {
            return restored;
        }
    }

    return result;
}

void PlateOcrCoordinator::pruneHistory(const qint64 nowMs)
{
    for (auto it = m_histories.begin(); it != m_histories.end();)
    {
        if ((nowMs - it.value().lastUpdatedMs) > kHistoryTtlMs)
        {
            it = m_histories.erase(it);
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

    for (WorkerState &worker : m_workers)
    {
        if (m_pending.isEmpty())
        {
            return;
        }
        if (worker.watcher.isRunning())
        {
            continue;
        }

        const PendingOcr next = m_pending.dequeue();
        const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
        const qint64 queueWaitMs =
            (next.enqueuedAtMs > 0 && startMs > next.enqueuedAtMs)
                ? (startMs - next.enqueuedAtMs)
                : 0;
        worker.runningObjectId = next.objectId;
        worker.queuedAtMs = next.enqueuedAtMs;
        worker.startedAtMs = startMs;
        m_inflightObjectIds.insert(next.objectId);
        WorkerState *workerPtr = &worker;

        qDebug() << "[OCR][Start] objectId=" << next.objectId
                 << "queueWaitMs=" << queueWaitMs
                 << "pending=" << m_pending.size();

        QFuture<QString> future = QtConcurrent::run(
            [workerPtr, objectId = next.objectId, crop = next.crop]()
            { return workerPtr->ocrManager.performOcr(crop, objectId); });
        worker.watcher.setFuture(future);
    }
}
