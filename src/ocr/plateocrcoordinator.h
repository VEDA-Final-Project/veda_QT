#ifndef PLATEOCRCOORDINATOR_H
#define PLATEOCRCOORDINATOR_H

#include "ocr/ocrmanager.h"
#include <QFutureWatcher>
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

  void startNext();

  OcrManager m_ocrManager;
  QFutureWatcher<QString> m_watcher;
  QQueue<PendingOcr> m_pending;
  QSet<int> m_inflightObjectIds;
  int m_runningObjectId = -1;
  bool m_shuttingDown = false;
};

#endif // PLATEOCRCOORDINATOR_H
