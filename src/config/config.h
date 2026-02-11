#ifndef CONFIG_H
#define CONFIG_H

#include <QJsonObject>
#include <QObject>
#include <QString>

/**
 * @brief 설정 관리 클래스
 *
 * config/settings.json 파일에서 설정을 로드하고 관리합니다.
 * 싱글톤 패턴으로 구현되어 앱 전체에서 동일한 인스턴스를 사용합니다.
 */
class Config : public QObject {
  Q_OBJECT

public:
  static Config &instance();

  bool load(const QString &path = "config/settings.json");

  // Camera
  QString cameraIp() const;
  QString cameraUsername() const;
  QString cameraPassword() const;
  QString cameraProfile() const;
  QString rtspUrl() const;

  // Video
  int sourceWidth() const;
  int sourceHeight() const;
  int effectiveWidth() const;
  int cropOffsetX() const;

  // OCR
  QString ocrLanguage() const;
  QString tessdataPath() const;

  // Sync
  int defaultDelayMs() const;

private:
  explicit Config(QObject *parent = nullptr);
  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;

  QJsonObject m_camera;
  QJsonObject m_video;
  QJsonObject m_ocr;
  QJsonObject m_sync;
  bool m_loaded = false;
};

#endif // CONFIG_H
