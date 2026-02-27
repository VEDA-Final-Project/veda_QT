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
    if (!m_ocrManager.init(cfg.ocrModelPath(), cfg.ocrDictPath(),
                           cfg.ocrInputWidth(), cfg.ocrInputHeight()))
    {
        qDebug() << "Could not initialize OCR Manager.";
    }

    connect(&m_watcher, &QFutureWatcher<QString>::finished, this,
            &PlateOcrCoordinator::onOcrFinished);
}

PlateOcrCoordinator::~PlateOcrCoordinator()
{
    m_shuttingDown = true;
    m_pending.clear();
    m_inflightObjectIds.clear();
    m_pendingObjectIds.clear();
    m_histories.clear();
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

    if (m_inflightObjectIds.contains(objectId) || m_pendingObjectIds.contains(objectId))
    {
        return;
    }

    m_pending.enqueue(PendingOcr{objectId, crop});
    m_pendingObjectIds.insert(objectId);
    startNext();
}

void PlateOcrCoordinator::onOcrFinished()
{
    if (m_shuttingDown)
    {
        return;
    }

    const QString result = m_watcher.result();
    const int objectId = m_runningObjectId;

    m_inflightObjectIds.remove(objectId);
    m_pendingObjectIds.remove(objectId);
    m_runningObjectId = -1;

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
    const QString stabilizedInput = restoreDigitOnlyResult(history, result);
    if (stabilizedInput != result)
    {
        qDebug() << "[OCR] Digit-only result restored from history. raw="
                 << result << "restored=" << stabilizedInput;
    }

    if (!kPlatePattern.match(stabilizedInput).hasMatch())
    {
        qDebug() << "[OCR] Skipping non-matching OCR result. raw=" << result
                 << "candidate=" << stabilizedInput;
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

    if (m_watcher.isRunning() || m_pending.isEmpty())
    {
        return;
    }

    const PendingOcr next = m_pending.dequeue();
    m_runningObjectId = next.objectId;
    m_inflightObjectIds.insert(next.objectId);

    QFuture<QString> future = QtConcurrent::run(
        [this, crop = next.crop]()
        { return m_ocrManager.performOcr(crop); });
    m_watcher.setFuture(future);
}
