#include "config.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>

namespace {
const QString kDefaultCameraKey = QStringLiteral("camera");
const QString kFallbackCameraProfile = QStringLiteral("profile6/media.smp");
const QString kFallbackCameraSubProfile = QStringLiteral("profile7/media.smp");

QString normalizedCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
}

bool isCameraObject(const QJsonObject &root, const QString &key) {
  return key.startsWith(QStringLiteral("camera")) && root.value(key).isObject();
}

QString profileValue(const QJsonObject &cameraObj, const QJsonObject &defaults,
                     const char *key, const QString &fallback) {
  const QString cameraValue = cameraObj.value(QLatin1String(key)).toString().trimmed();
  if (!cameraValue.isEmpty()) {
    return cameraValue;
  }
  const QString defaultValue =
      defaults.value(QLatin1String(key)).toString().trimmed();
  return defaultValue.isEmpty() ? fallback : defaultValue;
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

/**
 * @brief Config 싱글톤 인스턴스 반환
 * - 애플리케이션 전역에서 하나의 설정 인스턴스만 사용
 */
Config &Config::instance() {
  static Config instance;
  return instance;
}

/**
 * @brief Config 생성자
 */
Config::Config(QObject *parent) : QObject(parent) {}

/**
 * @brief 설정 파일 로드
 * @param path 설정 파일 경로 (상대/절대 가능)
 * @return 성공 시 true, 실패 시 false
 *
 * 여러 후보 경로를 순차적으로 탐색하여
 * 개발/배포 환경 모두에서 유연하게 동작하도록 설계됨
 */
bool Config::load(const QString &path) {
  QStringList candidates;

  // === 절대 경로인 경우 그대로 사용 ===
  if (QDir::isAbsolutePath(path)) {
    candidates << path;
  } else {
    /**
     * === 개발/배포 환경 대응용 경로 탐색 순서 ===
     * 1) 프로젝트 소스 디렉토리 (빌드 시스템 제공 시)
     * 2) 현재 작업 디렉토리
     * 3) 실행 파일 디렉토리
     * 4) 실행 파일 기준 상대 fallback
     */
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

  // === 후보 경로 순회하며 설정 파일 탐색 ===
  for (const QString &candidate : candidatePaths) {
    file.setFileName(candidate);
    if (file.open(QIODevice::ReadOnly)) {
      loadedPath = candidate;
      break;
    }
    qWarning() << "Cannot open config file:" << candidate;
  }

  // === 설정 파일을 하나도 찾지 못한 경우 ===
  if (loadedPath.isEmpty()) {
    return false;
  }

  // === 파일 전체 읽기 ===
  QByteArray data = file.readAll();
  file.close();

  // === JSON 파싱 ===
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError) {
    qWarning() << "JSON parse error:" << error.errorString();
    return false;
  }

  // === 루트 오브젝트에서 섹션별 설정 추출 ===
  QJsonObject root = doc.object();
  m_root = root;
  m_camera = cameraObjectForKey(root, kDefaultCameraKey);
  m_cameraDefaults = root["cameraDefaults"].toObject();
  m_video = root["video"].toObject();
  m_ocr = root["ocr"].toObject();
  m_reid = root["reid"].toObject();
  m_sync = root["sync"].toObject();
  m_auth = root["auth"].toObject();
  m_loadedConfigPath = loadedPath;

  qDebug() << "Config loaded from:" << loadedPath;
  return true;
}

/* =========================
 * Camera 관련 설정
 * ========================= */

QStringList Config::cameraKeys() const {
  const QStringList keys = cameraKeysFromRoot(m_root);
  return keys.isEmpty() ? QStringList{kDefaultCameraKey} : keys;
}

/**
 * @brief 카메라 IP 주소
 */
QString Config::cameraIp(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return (cameraObj.isEmpty() ? m_camera : cameraObj)["ip"].toString(
      "192.168.0.23");
}

/**
 * @brief 카메라 로그인 ID
 */
QString Config::cameraUsername(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return (cameraObj.isEmpty() ? m_camera : cameraObj)["username"].toString(
      "admin");
}

/**
 * @brief 카메라 로그인 비밀번호
 */
QString Config::cameraPassword(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return (cameraObj.isEmpty() ? m_camera : cameraObj)["password"].toString("");
}

QString Config::defaultCameraProfile() const {
  return profileValue(QJsonObject(), m_cameraDefaults, "profile",
                      kFallbackCameraProfile);
}

QString Config::defaultCameraSubProfile() const {
  return profileValue(QJsonObject(), m_cameraDefaults, "subProfile",
                      defaultCameraProfile().trimmed().isEmpty()
                          ? kFallbackCameraSubProfile
                          : defaultCameraProfile().trimmed());
}

/**
 * @brief RTSP 프로파일 경로
 */
QString Config::cameraProfile(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  return profileValue(cameraObj.isEmpty() ? m_camera : cameraObj,
                      m_cameraDefaults, "profile", defaultCameraProfile());
}

QString Config::cameraSubProfile(const QString &cameraKey) const {
  const QJsonObject cameraObj = cameraObjectForKey(m_root, cameraKey);
  const QJsonObject selectedCamera = cameraObj.isEmpty() ? m_camera : cameraObj;
  const QString fallbackProfile = cameraProfile(cameraKey);
  return profileValue(selectedCamera, m_cameraDefaults, "subProfile",
                      fallbackProfile);
}

/**
 * @brief RTSP 접속 URL 생성
 */
/* =========================
 * Video 관련 설정
 * ========================= */

/**
 * @brief 원본 영상 너비
 */
int Config::sourceWidth() const { return m_video["sourceWidth"].toInt(3840); }

/**
 * @brief 원본 영상 높이
 */
int Config::sourceHeight() const { return m_video["sourceHeight"].toInt(2160); }

/**
 * @brief 실제 처리에 사용할 영상 너비
 * (크롭 이후 유효 영역)
 */
int Config::effectiveWidth() const {
  return m_video["effectiveWidth"].toInt(2880);
}

/**
 * @brief 영상 크롭 시작 X 좌표
 */
int Config::cropOffsetX() const { return m_video["cropOffsetX"].toInt(480); }

/* =========================
 * OCR 관련 설정
 * ========================= */

QString Config::ocrType() const {
  return m_ocr["type"].toString("Paddle"); // 기본값은 Paddle
}

QString Config::ocrModelPath() const {
  const QString path = m_ocr["modelPath"].toString();
  if (path.isEmpty() || path == "null") {
    return QString();
  }
  return resolveConfigRelativePath(path);
}

QString Config::ocrDictPath() const {
  const QString path = m_ocr["dictPath"].toString();
  if (path.isEmpty() || path == "null") {
    return QString();
  }
  return resolveConfigRelativePath(path);
}

int Config::ocrInputWidth() const {
  return std::max(16, m_ocr["inputWidth"].toInt(320));
}

int Config::ocrInputHeight() const {
  return std::max(16, m_ocr["inputHeight"].toInt(48));
}

QString Config::reidModelPath() const {
  const QString path = m_reid["modelPath"].toString();
  if (path.isEmpty() || path == "null") {
    return QString();
  }
  return resolveConfigRelativePath(path);
}

int Config::reidInputWidth() const {
  return std::max(32, m_reid["inputWidth"].toInt(256));
}

int Config::reidInputHeight() const {
  return std::max(32, m_reid["inputHeight"].toInt(256));
}

QString Config::resolveConfigRelativePath(const QString &path) const {
  const QString trimmed = path.trimmed();
  if (trimmed.isEmpty()) {
    return QString();
  }
  if (QDir::isAbsolutePath(trimmed) || m_loadedConfigPath.isEmpty()) {
    return trimmed;
  }

  const QFileInfo configInfo(m_loadedConfigPath);
  const QDir configDir = configInfo.dir();
  const QString configRelativePath = configDir.absoluteFilePath(trimmed);
  if (QFileInfo::exists(configRelativePath)) {
    return configRelativePath;
  }

  QDir parentDir = configDir;
  if (parentDir.cdUp()) {
    const QString parentRelativePath = parentDir.absoluteFilePath(trimmed);
    if (QFileInfo::exists(parentRelativePath)) {
      return parentRelativePath;
    }
    return parentRelativePath;
  }

  return configRelativePath;
}

/* =========================
 * Gemini 관련 설정
 * ========================= */

QString Config::geminiApiKey() const {
  // 환경변수 우선 참조
  QString key = qEnvironmentVariable("GEMINI_API_KEY");
  if (!key.isEmpty()) {
    // qInfo() << "[Config] Gemini API Key loaded from Environment (Masked: " << key.left(5) << "...)";
    return key;
  }
  QString jsonKey = m_ocr["gemini"].toObject()["apiKey"].toString();
  if (jsonKey.isEmpty()) {
    // qWarning() << "[Config] Gemini API Key is missing!";
  }
  return jsonKey;
}

QString Config::geminiModel() const {
  return m_ocr["gemini"].toObject()["model"].toString("gemini-1.5-flash");
}

QString Config::geminiPrompt() const {
  return m_ocr["gemini"].toObject()["prompt"].toString(
      "이 이미지는 자동차 번호판의 크롭 본이야. 번호판 숫자와 글자만 정확히 추출해줘. "
      "다른 설명은 필요 없어. 예: 123가4567");
}

/* =========================
 * Sync 관련 설정
 * ========================= */

/**
 * @brief 기본 동기화 지연 시간 (ms)
 */
int Config::defaultDelayMs() const { return m_sync["defaultDelayMs"].toInt(0); }

/* =========================
 * Auth 관련 설정
 * ========================= */

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
