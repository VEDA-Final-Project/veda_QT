#ifndef CONFIG_H
#define CONFIG_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

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
  QStringList cameraKeys() const;
  QString cameraIp(const QString &cameraKey = QString()) const;
  QString cameraUsername(const QString &cameraKey = QString()) const;
  QString cameraPassword(const QString &cameraKey = QString()) const;
  QString defaultCameraProfile() const;
  QString defaultCameraSubProfile() const;
  QString cameraProfile(const QString &cameraKey = QString()) const;
  QString cameraSubProfile(const QString &cameraKey = QString()) const;

  // Video
  int sourceWidth() const;
  int sourceHeight() const;
  int effectiveWidth() const;
  int cropOffsetX() const;

  // OCR
  QString ocrType() const;
  QString ocrModelPath() const;
  QString ocrDictPath() const;
  int ocrInputWidth() const;
  int ocrInputHeight() const;

  // ReID
  QString reidModelPath() const;
  int reidInputWidth() const;
  int reidInputHeight() const;

  // Gemini
  QString geminiApiKey() const;
  QString geminiModel() const;
  QString geminiPrompt() const;

  // Sync
  int defaultDelayMs() const;

  // Auth
  QString authHost() const;
  int authPort() const;
  int authConnectTimeoutMs() const;
  int authRequestTimeoutMs() const;

private:
  explicit Config(QObject *parent = nullptr);
  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;
  QString resolveConfigRelativePath(const QString &path) const;

  QJsonObject m_root;
  QJsonObject m_camera;
  QJsonObject m_cameraDefaults;
  QJsonObject m_video;
  QJsonObject m_ocr;
  QJsonObject m_reid;
  QJsonObject m_sync;
  QJsonObject m_auth;
  QString m_loadedConfigPath;
};

#endif // CONFIG_H
