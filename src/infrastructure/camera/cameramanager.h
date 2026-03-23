#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include "infrastructure/metadata/metadatathread.h"
#include "infrastructure/video/videothread.h"
#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>

struct CameraConnectionInfo {
  QString cameraId;
  QString ip;
  QString username;
  QString password;
  QString profile;
  QString subProfile;

  bool isValid() const { return !ip.isEmpty() && !username.isEmpty(); }
};

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

  void setConnectionInfo(const CameraConnectionInfo &connectionInfo);
  void start();
  void startDisplayOnly();
  void startAnalytics();
  void stopAnalytics();
  void stop();
  void setTargetFps(int fps);
  void restart();
  void restartDisplayPipeline();
  bool isRunning() const;
  bool isDisplayRunning() const;
  bool isAnalyticsRunning() const;

  void setDisabledObjectTypes(const QSet<QString> &types);

signals:
  void frameCaptured(SharedVideoFrame frame);
  void metadataReceived(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);

private:
  void createDisplayThread();
  void createAnalyticsThreads();
  void startDisplayPipeline();
  void startAnalyticsPipeline();
  void stopThread(QThread *thread, const QString &name, bool warnOnFailure);

  CameraConnectionInfo m_connectionInfo;
  QSet<QString> m_disabledTypes;
  VideoThread *m_videoThread;
  MetadataThread *m_metadataThread;
};

#endif // CAMERAMANAGER_H
