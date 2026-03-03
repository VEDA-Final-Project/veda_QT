#ifndef PLATEOCRCOORDINATOR_H
#define PLATEOCRCOORDINATOR_H

#include "ocr/ocrmanager.h"
#include <array>
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

private:
    static constexpr size_t kWorkerCount = 2;

    struct PendingOcr
    {
        int objectId = -1;
        QImage crop;
        qint64 enqueuedAtMs = 0;
    };

    struct WorkerState
    {
        OcrManager ocrManager;
        QFutureWatcher<QString> watcher;
        int runningObjectId = -1;
        qint64 queuedAtMs = 0;
        qint64 startedAtMs = 0;
    };

    struct OcrHistory
    {
        QQueue<QString> recentResults;
        QString lastEmitted;
        qint64 lastUpdatedMs = 0;
        int logFrameCount = 0;
    };

    QString restoreDigitOnlyResult(const OcrHistory &history,
                                   const QString &result) const;
    QString stabilizeResult(int objectId, const QString &result);
    void pruneHistory(qint64 nowMs);
    void onWorkerFinished(size_t workerIndex);
    void startNext();

    std::array<WorkerState, kWorkerCount> m_workers;
    QQueue<PendingOcr> m_pending;
    QSet<int> m_inflightObjectIds;
    QSet<int> m_pendingObjectIds;
    QHash<int, OcrHistory> m_histories;
    bool m_shuttingDown = false;
};

#endif // PLATEOCRCOORDINATOR_H
