#include "config.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStringList>

Config &Config::instance()
{
  static Config instance;
  return instance;
}

Config::Config(QObject *parent) : QObject(parent) {}

bool Config::load(const QString &path)
{
  QStringList candidates;
  if (QDir::isAbsolutePath(path))
  {
    candidates << path;
  }
  else
  {
    // Development-friendly order:
    // 1) project source directory (if provided by build system)
    // 2) current working directory
    // 3) executable directory
    // 4) executable-relative fallback
#ifdef PROJECT_SOURCE_DIR
    candidates << QDir(QStringLiteral(PROJECT_SOURCE_DIR)).absoluteFilePath(path);
#endif
    candidates << QDir::current().absoluteFilePath(path)
               << QCoreApplication::applicationDirPath() + "/" + path
               << QCoreApplication::applicationDirPath() +
                      "/../../config/settings.json";
  }

  QFile file;
  QString loadedPath;
  for (const auto &candidate : candidates)
  {
    file.setFileName(candidate);
    if (file.open(QIODevice::ReadOnly))
    {
      loadedPath = candidate;
      break;
    }
    qWarning() << "Cannot open config file:" << candidate;
  }

  if (loadedPath.isEmpty())
  {
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError)
  {
    qWarning() << "JSON parse error:" << error.errorString();
    return false;
  }

  QJsonObject root = doc.object();
  m_camera = root["camera"].toObject();
  m_video = root["video"].toObject();
  m_ocr = root["ocr"].toObject();
  m_sync = root["sync"].toObject();
  m_loaded = true;

  qDebug() << "Config loaded from:" << loadedPath;
  return true;
}

// Camera
QString Config::cameraIp() const
{
  return m_camera["ip"].toString("192.168.0.23");
}

QString Config::cameraUsername() const
{
  return m_camera["username"].toString("admin");
}

QString Config::cameraPassword() const
{
  return m_camera["password"].toString("");
}

QString Config::cameraProfile() const
{
  return m_camera["profile"].toString("profile2/media.smp");
}

QString Config::rtspUrl() const
{
  return QString("rtsp://%1:%2@%3/%4")
      .arg(cameraUsername(), cameraPassword(), cameraIp(), cameraProfile());
}

// Video
int Config::sourceWidth() const { return m_video["sourceWidth"].toInt(3840); }

int Config::sourceHeight() const { return m_video["sourceHeight"].toInt(2160); }

int Config::effectiveWidth() const
{
  return m_video["effectiveWidth"].toInt(2880);
}

int Config::cropOffsetX() const { return m_video["cropOffsetX"].toInt(480); }

// OCR
QString Config::ocrLanguage() const
{
  return m_ocr["language"].toString("kor");
}

QString Config::tessdataPath() const
{
  QString path = m_ocr["tessdataPath"].toString();
  if (path.isEmpty() || path == "null")
  {
    return QString(); // use built-in default path
  }
  return path;
}

// Sync
int Config::defaultDelayMs() const
{
  return m_sync["defaultDelayMs"].toInt(0);
}
