#include "cameramanager.h"
#include <QDebug>
#include <QDateTime>
#include <QElapsedTimer>
#include "infrastructure/camera/rtspurl.h"

namespace {
constexpr unsigned long kThreadStopTimeoutMs = 2000;
} // namespace

CameraManager::CameraManager(QObject *parent)
    : QObject(parent), m_videoThread(nullptr), m_metadataThread(nullptr), m_srtpOrchestrator(nullptr) {
}

CameraManager::~CameraManager() { stop(); }

void CameraManager::createDisplayThread() {
  if (!m_videoThread) {
    m_videoThread = new VideoThread(this);
    connect(m_videoThread, &VideoThread::frameCaptured, this, &CameraManager::frameCaptured);
    connect(m_videoThread, &VideoThread::logMessage, this, &CameraManager::logMessage);
  }
}

void CameraManager::createAnalyticsThreads() {
  if (!m_metadataThread) {
    m_metadataThread = new MetadataThread(this);
    m_metadataThread->setDisabledTypes(m_disabledTypes);
    connect(m_metadataThread, &MetadataThread::metadataReceived, this,
            &CameraManager::metadataReceived);
    connect(m_metadataThread, &MetadataThread::logMessage, this, &CameraManager::logMessage);
  }
}

void CameraManager::setConnectionInfo(const CameraConnectionInfo &connectionInfo) {
  m_connectionInfo = connectionInfo;
}

void CameraManager::startDisplayPipeline() {
  if (m_connectionInfo.srtpEnabled) {
    if (!m_srtpOrchestrator) {
      m_srtpOrchestrator = new SrtpOrchestrator(this);
      connect(m_srtpOrchestrator, &SrtpOrchestrator::frameCaptured, this, &CameraManager::frameCaptured);
      connect(m_srtpOrchestrator, &SrtpOrchestrator::metadataReceived, this,
              [this](const QList<ObjectInfo> &objects) {
                emit metadataReceived(objects, QDateTime::currentMSecsSinceEpoch());
              });
      connect(m_srtpOrchestrator, &SrtpOrchestrator::logMessage, this, &CameraManager::logMessage);
    }
    m_srtpOrchestrator->setDisabledObjectTypes(m_disabledTypes);
    m_srtpOrchestrator->setConnectionInfo(
        m_connectionInfo.ip, m_connectionInfo.username,
        m_connectionInfo.password, m_connectionInfo.profile,
        m_connectionInfo.allowedFingerprints);
    m_srtpOrchestrator->start();
    return;
  }
  createDisplayThread();
  if (!m_connectionInfo.isValid()) return;
  m_videoThread->setUrl(buildRtspUrl(m_connectionInfo.ip, m_connectionInfo.username, m_connectionInfo.password, m_connectionInfo.profile));
  m_videoThread->start();
}

void CameraManager::startAnalyticsPipeline() {
  if (m_connectionInfo.srtpEnabled) return;
  createAnalyticsThreads();
  if (!m_connectionInfo.isValid()) return;
  m_metadataThread->setConnectionInfo(m_connectionInfo.ip, m_connectionInfo.username, m_connectionInfo.password, m_connectionInfo.subProfile.isEmpty() ? m_connectionInfo.profile : m_connectionInfo.subProfile);
  if (!m_metadataThread->isRunning()) m_metadataThread->start();
}

void CameraManager::start() { startDisplayOnly(); startAnalytics(); }
void CameraManager::startDisplayOnly() { if (!isDisplayRunning()) startDisplayPipeline(); }
void CameraManager::startAnalytics() { if (!isAnalyticsRunning()) startAnalyticsPipeline(); }

void CameraManager::stop() {
  if (m_videoThread) { m_videoThread->stop(); m_videoThread->deleteLater(); m_videoThread = nullptr; }
  if (m_metadataThread) { m_metadataThread->stop(); m_metadataThread->deleteLater(); m_metadataThread = nullptr; }
  if (m_srtpOrchestrator) { m_srtpOrchestrator->stop(); m_srtpOrchestrator->deleteLater(); m_srtpOrchestrator = nullptr; }
}

bool CameraManager::isRunning() const { return isDisplayRunning() || isAnalyticsRunning(); }
bool CameraManager::isDisplayRunning() const {
  return m_connectionInfo.srtpEnabled
             ? (m_srtpOrchestrator && m_srtpOrchestrator->isRunning())
             : (m_videoThread && m_videoThread->isRunning());
}
bool CameraManager::isAnalyticsRunning() const {
  return m_connectionInfo.srtpEnabled
             ? (m_srtpOrchestrator && m_srtpOrchestrator->isRunning())
             : (m_metadataThread && m_metadataThread->isRunning());
}
void CameraManager::stopAnalytics() { if (m_metadataThread) { m_metadataThread->stop(); m_metadataThread->deleteLater(); m_metadataThread = nullptr; } }
void CameraManager::setTargetFps(int fps) { if (m_videoThread) m_videoThread->setTargetFps(fps); }
void CameraManager::setDisabledObjectTypes(const QSet<QString> &types) {
  m_disabledTypes = types;
  if (m_metadataThread) {
    m_metadataThread->setDisabledTypes(m_disabledTypes);
  }
  if (m_srtpOrchestrator) {
    m_srtpOrchestrator->setDisabledObjectTypes(m_disabledTypes);
  }
}
void CameraManager::restart() { stop(); start(); }
void CameraManager::restartDisplayPipeline() { stop(); startDisplayPipeline(); }
void CameraManager::stopThread(QThread *thread, const QString &name, bool warn) { if (thread && thread->isRunning()) { thread->quit(); thread->wait(kThreadStopTimeoutMs); } }
