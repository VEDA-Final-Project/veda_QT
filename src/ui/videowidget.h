#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include "config.h"
#include "metadatathread.h"
#include "ocrmanager.h"
#include <QDateTime>
#include <QFutureWatcher>
#include <QLabel>
#include <QQueue>
#include <QWidget>
#include <QtConcurrent/QtConcurrent>

/**
 * @brief 비디오 렌더링 위젯
 *
 * 비디오 프레임에 메타데이터 오버레이를 그리고 OCR 처리를 수행합니다.
 * MainWindow에서 비디오 렌더링 로직을 분리합니다.
 */
class VideoWidget : public QLabel {
  Q_OBJECT

public:
  explicit VideoWidget(QWidget *parent = nullptr);
  ~VideoWidget();

  void setSyncDelay(int delayMs);
  int syncDelay() const;

public slots:
  void updateFrame(const QImage &frame);
  void updateMetadata(const QList<ObjectInfo> &objects);

signals:
  void ocrResult(int objectId, const QString &result);

private slots:
  void onOcrFinished();

private:
  void renderFrame(const QImage &frame);

  // 메타데이터 싱크
  QList<ObjectInfo> m_currentObjects;
  QQueue<QPair<qint64, QList<ObjectInfo>>> m_metadataQueue;
  int m_syncDelayMs = 0;

  // OCR (serialized; Tesseract is not used concurrently).
  OcrManager *m_ocrManager = nullptr;
  QFutureWatcher<QString> m_ocrWatcher;
  int m_processingOcrId = -1;
};

#endif // VIDEOWIDGET_H


