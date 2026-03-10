#include "config.h"
#include "util/rtspurl.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>

namespace {
const QString kDefaultCameraKey = QStringLiteral("camera");

QString normalizedCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
}

bool isCameraObject(const QJsonObject &root, const QString &key) {
  return key.startsWith(QStringLiteral("camera")) && root.value(key).isObject();
}

QJsonObject cameraObjectForKey(const QJsonObject &root,
                               const QString &cameraKey) {
  const QString key = normalizedCameraKey(cameraKey);
  if (root.value(key).isObject()) {
    return root.value(key).toObject();
  }
  if (key != kDefaultCameraKey && root.value(kDefaultCameraKey).isObject()) {
    return root.value(kDefaultCameraKey).toObject();
  }
  return QJsonObject();
}

QStringList cameraKeysFromRoot(const QJsonObject &root) {
  QStringList keys;
  if (root.value(kDefaultCameraKey).isObject()) {
    keys.append(kDefaultCameraKey);
  }

  QStringList others;
  for (const QString &key : root.keys()) {
    if (key == kDefaultCameraKey) {
      continue;
    }
    if (isCameraObject(root, key)) {
      others.append(key);
    }
  }
  std::sort(others.begin(), others.end());
  keys.append(others);
  return keys;
}
} // namespace

Config &Config::instance() {
  static Config instance;
  return instance;
}

Config::Config(QObject *parent) : QObject(parent) {}

bool Config::load(const QString &path) {
  QStringList candidates;

  if (QDir::isAbsolutePath(path)) {
    candidates << path;
  } else {

#ifdef PROJECT_SOURCE_DIR
    candidates
        << QDir(QStringLiteral(PROJECT_SOURCE_DIR)).absoluteFilePath(path);
#endif
    candidates << QDir::current().absoluteFilePath(path)
               << QCoreApplication::applicationDirPath() + "/" + path
               << QCoreApplication::applicationDirPath() +
                      "/../../config/settings.json";
  }

  QFile file;
  QString loadedPath;
  const QStringList &candidatePaths = candidates;
  for (const QString &candidate : candidatePaths) {
    file.setFileName(candidate);
    if (file.open(QIODevice::ReadOnly)) {
      loadedPath = candidate;
      break;
    }
    qWarning() << "Cannot open config file:" << candidate;
  }

  if (loadedPath.isEmpty()) {
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "JSON parse error:" << error.errorString();
    return false;
  }

  QJsonObject root = doc.object();
  m_root = root;
  m_camera = cameraObjectForKey(root, kDefaultCameraKey);
  m_video = root["video"].toObject();
  m_ocr = root["ocr"].toObject();
  m_sync = root["sync"].toObject();
  m_auth = root["auth"].toObject();
  m_loaded = true;

  qDebug() << "Config loaded from:" << loadedPath;
  return true;
}

// ── Camera ──

QStringList Config::cameraKeys() const {
  const QStringList keys = cameraKeysFromRoot(m_root);
  return keys.isEmpty() ? QStringList{kDefaultCameraKey} : keys;
}

QString Config::cameraIp(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return (cameraObj.isEmpty() ? m_camera : cameraObj)["ip"].toString(
      "192.168.0.23");
}

QString Config::cameraUsername(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return (cameraObj.isEmpty() ? m_camera : cameraObj)["username"].toString(
      "admin");
}

QString Config::cameraPassword(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return (cameraObj.isEmpty() ? m_camera : cameraObj)["password"].toString("");
}

QString Config::cameraProfile(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return (cameraObj.isEmpty() ? m_camera : cameraObj)["profile"].toString(
      "profile2/media.smp");
}

QString Config::cameraSubProfile(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  const QJsonObject selectedCamera = cameraObj.isEmpty() ? m_camera : cameraObj;
  const QString fallbackProfile =
      selectedCamera["profile"].toString("profile2/media.smp");
  return selectedCamera["subProfile"].toString(fallbackProfile);
}

QString Config::rtspUrl(const QString &cameraKey) const {
  return buildRtspUrl(cameraIp(cameraKey), cameraUsername(cameraKey),
                      cameraPassword(cameraKey), cameraProfile(cameraKey));
}

// ── Video ──

int Config::sourceWidth() const { return m_video["sourceWidth"].toInt(3840); }

int Config::sourceHeight() const { return m_video["sourceHeight"].toInt(2160); }

int Config::effectiveWidth() const {
  return m_video["effectiveWidth"].toInt(2880);
}

int Config::cropOffsetX() const { return m_video["cropOffsetX"].toInt(480); }

// ── OCR ──

QString Config::ocrModelPath() const {
  const QString path = m_ocr["modelPath"].toString();
  if (path.isEmpty() || path == "null") {
    return QString();
  }
  return path;
}

QString Config::ocrDictPath() const {
  const QString path = m_ocr["dictPath"].toString();
  if (path.isEmpty() || path == "null") {
    return QString();
  }
  return path;
}

int Config::ocrInputWidth() const {
  return std::max(16, m_ocr["inputWidth"].toInt(320));
}

int Config::ocrInputHeight() const {
  return std::max(16, m_ocr["inputHeight"].toInt(48));
}

// ── Sync ──

int Config::defaultDelayMs() const { return m_sync["defaultDelayMs"].toInt(0); }

// ── Auth ──

QString Config::authHost() const {
  return m_auth["host"].toString(QStringLiteral("192.168.0.67"));
}

int Config::authPort() const { return m_auth["port"].toInt(9000); }

int Config::authConnectTimeoutMs() const {
  return m_auth["connectTimeoutMs"].toInt(3000);
}

int Config::authRequestTimeoutMs() const {
  return m_auth["requestTimeoutMs"].toInt(5000);
}
