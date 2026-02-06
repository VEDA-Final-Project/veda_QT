
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "metadatathread.h"
#include "ocrmanager.h"
#include "videothread.h"
#include <QDateTime>
#include <QLabel>
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QQueue>

#include <QFutureWatcher>
#include <QTextEdit>
#include <QtConcurrent/QtConcurrent>

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

public slots:
  void close();

private slots:
  void playCctv();
  void updateFrame(const QImage &frame);
  void updateMetadata(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);
  void decreaseSync(); // -100ms
  void increaseSync(); // +100ms

  // OCR 완료 시 호출될 슬롯
  void onOcrFinished();

private:
  void updateSyncLabel();

  VideoThread *m_videoThread;
  MetadataThread *m_metadataThread;
  QLabel *m_videoLabel;
  QTextEdit *m_logView;

  // 싱크 조절 UI
  QLabel *m_lblSync;

  // 메타데이터 큐잉 및 싱크
  QList<ObjectInfo> m_currentObjects;

  // <수신시간(ms), 객체리스트>
  QQueue<QPair<qint64, QList<ObjectInfo>>> m_metadataQueue;

  // OCR Engine
  OcrManager *m_ocrManager;
  // 비동기 실행을 위한 Watcher
  QFutureWatcher<QString> m_ocrWatcher;
  // OCR 결과를 UI에 표시하기 위해 잠시 저장하는 변수 (필요 시)
  // 하지만 watcher가 결과를 가지고 있으므로 생략하거나,
  // 어떤 Object에 대한 결과인지 알기 위해 ID를 저장할 필요가 있음.
  int m_processingOcrId = -1; // 현재 처리 중인 객체 ID
  int m_syncDelayMs;          // 메타데이터 표시 지연 시간 (ms)
};
#endif // MAINWINDOW_H
