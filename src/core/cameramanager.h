#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include "core/config.h"
#include "metadata/metadatathread.h"
#include "video/videothread.h"
#include <QObject>

/**
 * @brief 카메라 연결 관리 클래스
 *
 * RTSP 비디오 스트림과 메타데이터 스트림의 시작/중지를 관리합니다.
 * MainWindow에서 카메라 관련 로직을 분리하여 단일 책임 원칙을 준수합니다.
 */

class CameraManager : public QObject {
  Q_OBJECT

public:
  explicit CameraManager(QObject *parent = nullptr);
  ~CameraManager();

  void start();
  void stop();
  void restart();
  bool isRunning() const;

signals:
  void frameCaptured(const QImage &image);
  void metadataReceived(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);

private:
  VideoThread *m_videoThread;
  MetadataThread *m_metadataThread;
};

#endif // CAMERAMANAGER_H
