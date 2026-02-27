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

    struct OcrHistory
    {
        QQueue<QString> recentResults;
        QString lastEmitted;
        qint64 lastUpdatedMs = 0;
    };

    QString restoreDigitOnlyResult(const OcrHistory &history,
                                   const QString &result) const;
    QString stabilizeResult(int objectId, const QString &result);
    void pruneHistory(qint64 nowMs);
    void startNext();

    OcrManager m_ocrManager;
    QFutureWatcher<QString> m_watcher;
    QQueue<PendingOcr> m_pending;
    QSet<int> m_inflightObjectIds;
    QSet<int> m_pendingObjectIds;
    QHash<int, OcrHistory> m_histories;
    int m_runningObjectId = -1;
    bool m_shuttingDown = false;
};

#endif // PLATEOCRCOORDINATOR_H
