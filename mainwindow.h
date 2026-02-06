
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QList>
#include "videothread.h"
#include "metadatathread.h"

#include <QTextEdit>

class MainWindow : public QMainWindow
{
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
    QList<QPair<qint64, QList<ObjectInfo>>> m_metadataQueue;
    int m_syncDelayMs; // 메타데이터 표시 지연 시간 (ms)
};
#endif // MAINWINDOW_H
