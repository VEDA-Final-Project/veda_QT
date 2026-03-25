#include "metadatathread.h"
#include "config/config.h"
#include "infrastructure/camera/rtspurl.h"
#include "infrastructure/metadata/ffmpegmetadatastreamreader.h"
#include "infrastructure/metadata/onvifmetadataparser.h"

#include <QMutexLocker>

namespace {
constexpr unsigned long kReconnectBaseDelayMs = 1000;
constexpr unsigned long kReconnectMaxDelayMs = 10000;
constexpr int kReconnectMaxExponent = 3;
constexpr unsigned long kReconnectPollSliceMs = 100;

bool sleepWithInterruption(QThread *thread, unsigned long totalMs) {
  unsigned long remainingMs = totalMs;
  while (remainingMs > 0) {
    if (!thread || thread->isInterruptionRequested()) {
      return false;
    }

    const unsigned long sleepMs =
        qMin(remainingMs, kReconnectPollSliceMs);
    QThread::msleep(sleepMs);
    remainingMs -= sleepMs;
  }

  return !thread || !thread->isInterruptionRequested();
}
} // namespace

MetadataThread::MetadataThread(QObject *parent)
    : QThread(parent) {}

MetadataThread::~MetadataThread()
{
  stop();
  wait();
}

/**
 * @brief 카메라 접속 정보 설정
 * @param ip 카메라 IP
 * @param user 사용자 ID
 * @param password 비밀번호
 * @param profile RTSP 프로파일 경로
 */
void MetadataThread::setConnectionInfo(const QString &ip, const QString &user,
                                       const QString &password,
                                       const QString &profile)
{
  // === 멀티스레드 안전을 위한 뮤텍스 보호 ===
  QMutexLocker locker(&m_mutex);

  m_ip = ip;
  m_user = user;
  m_password = password;
  m_profile = profile;

  // 앞에 '/'가 있으면 제거 (중복 슬래시 방지)
  if (m_profile.startsWith('/'))
  {
    m_profile.remove(0, 1);
  }
  // profile이 비어있으면 기본값 사용
  if (m_profile.isEmpty())
  {
    m_profile = Config::instance().defaultCameraProfile();
  }
}

void MetadataThread::setDisabledTypes(const QSet<QString> &types)
{
  QMutexLocker locker(&m_mutex);
  m_disabledTypes = types;
}

void MetadataThread::stop()
{
  requestInterruption();
  quit();
}

void MetadataThread::run()
{
  OnvifMetadataParser parser;
  parser.setTypeFilter([this](const QString &type) {
    QMutexLocker locker(&m_mutex);
    return m_disabledTypes.contains(type);
  });

  connect(&parser, &OnvifMetadataParser::metadataReceived, this,
          &MetadataThread::metadataReceived, Qt::DirectConnection);
  connect(&parser, &OnvifMetadataParser::logMessage, this,
          &MetadataThread::logMessage, Qt::DirectConnection);

  int retryExponent = 0;
  while (!isInterruptionRequested()) {
    QString ip;
    QString user;
    QString password;
    QString profile;
    {
      QMutexLocker locker(&m_mutex);
      ip = m_ip;
      user = m_user;
      password = m_password;
      profile = m_profile;
    }

    if (ip.isEmpty() || user.isEmpty()) {
      emit logMessage(
          "Metadata thread has no valid connection info; stopping retries.");
      return;
    }

    const QString url = buildRtspUrl(ip, user, password, profile);
    emit logMessage(retryExponent == 0
                        ? QStringLiteral(
                              "Starting metadata extraction via FFmpeg libraries...")
                        : QStringLiteral(
                              "Retrying metadata extraction via FFmpeg libraries..."));
    emit logMessage(QString("Metadata RTSP URL: %1").arg(maskedRtspUrl(url)));

    parser.reset();
    FfmpegMetadataStreamReader reader(
        url, [this]() { return isInterruptionRequested(); });
    const bool completed = reader.run(
        [&parser](const QByteArray &xmlChunk) { parser.pushData(xmlChunk); },
        [this](const QString &msg) { emit logMessage(msg); });

    if (isInterruptionRequested()) {
      break;
    }

    retryExponent = completed ? 0 : qMin(retryExponent + 1, kReconnectMaxExponent);
    const unsigned long delayMs =
        qMin(kReconnectMaxDelayMs,
             kReconnectBaseDelayMs * (1UL << retryExponent));

    emit logMessage(
        QString("Metadata stream stopped. Reconnecting in %1 ms.")
            .arg(delayMs));
    if (!sleepWithInterruption(this, delayMs)) {
      break;
    }
  }
}
