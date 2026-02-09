#include "cameramanager.h"

CameraManager::CameraManager(QObject *parent) : QObject(parent) {
  m_videoThread = new VideoThread(this);
  m_metadataThread = new MetadataThread(this);

  // 시그널 전달 (내부 스레드 -> 외부)
  connect(m_videoThread, &VideoThread::frameCaptured, this,
          &CameraManager::frameCaptured);
  connect(m_metadataThread, &MetadataThread::metadataReceived, this,
          &CameraManager::metadataReceived);
  connect(m_metadataThread, &MetadataThread::logMessage, this,
          &CameraManager::logMessage);
}

CameraManager::~CameraManager() {
  stop();
}

void CameraManager::start() {
  if (isRunning())
    return;

  // Reload settings on each start so runtime IP/config edits are reflected.
  if (!Config::instance().load()) {
    emit logMessage("Warning: could not reload config; using existing values.");
  }

  const auto &cfg = Config::instance();
  QString ip = cfg.cameraIp();
  QString id = cfg.cameraUsername();
  QString pw = cfg.cameraPassword();
  QString profile = cfg.cameraProfile();
  // Use the same profile for metadata to stay in sync.
  QString url = cfg.rtspUrl();
  emit logMessage(QString("Starting camera with IP=%1, profile=%2")
                      .arg(ip, profile));
  emit logMessage(QString("RTSP URL: %1").arg(url));

  // 비디오 시작
  m_videoThread->setUrl(url);
  m_videoThread->start();

  // 메타데이터 시작
  m_metadataThread->setConnectionInfo(ip, id, pw, profile);
  m_metadataThread->start();
}

void CameraManager::restart() {
  stop();
  start();
}

void CameraManager::stop() {
  if (m_videoThread->isRunning()) {
    m_videoThread->stop();
    m_videoThread->wait();
  }
  if (m_metadataThread->isRunning()) {
    m_metadataThread->stop();
    m_metadataThread->wait();
  }
}

bool CameraManager::isRunning() const {
  return m_videoThread->isRunning() || m_metadataThread->isRunning();
}

