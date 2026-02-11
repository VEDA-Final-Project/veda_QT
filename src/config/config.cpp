#include "config.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStringList>

/**
 * @brief Config 싱글톤 인스턴스 반환
 * - 애플리케이션 전역에서 하나의 설정 인스턴스만 사용
 */
Config &Config::instance()
{
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
bool Config::load(const QString &path)
{
    QStringList candidates;

    // === 절대 경로인 경우 그대로 사용 ===
    if (QDir::isAbsolutePath(path))
    {
        candidates << path;
    }
    else
    {
        /**
     * === 개발/배포 환경 대응용 경로 탐색 순서 ===
     * 1) 프로젝트 소스 디렉토리 (빌드 시스템 제공 시)
     * 2) 현재 작업 디렉토리
     * 3) 실행 파일 디렉토리
     * 4) 실행 파일 기준 상대 fallback
     */
#ifdef PROJECT_SOURCE_DIR
        candidates << QDir(QStringLiteral(PROJECT_SOURCE_DIR))
                          .absoluteFilePath(path);
#endif
        candidates << QDir::current().absoluteFilePath(path)
                   << QCoreApplication::applicationDirPath() + "/" + path
                   << QCoreApplication::applicationDirPath() +
                          "/../../config/settings.json";
    }

    QFile file;
    QString loadedPath;

    // === 후보 경로 순회하며 설정 파일 탐색 ===
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

    // === 설정 파일을 하나도 찾지 못한 경우 ===
    if (loadedPath.isEmpty())
    {
        return false;
    }

    // === 파일 전체 읽기 ===
    QByteArray data = file.readAll();
    file.close();

    // === JSON 파싱 ===
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError)
    {
        qWarning() << "JSON parse error:" << error.errorString();
        return false;
    }

    // === 루트 오브젝트에서 섹션별 설정 추출 ===
    QJsonObject root = doc.object();
    m_camera = root["camera"].toObject();
    m_video  = root["video"].toObject();
    m_ocr    = root["ocr"].toObject();
    m_sync   = root["sync"].toObject();
    m_loaded = true;

    qDebug() << "Config loaded from:" << loadedPath;
    return true;
}

/* =========================
 * Camera 관련 설정
 * ========================= */

/**
 * @brief 카메라 IP 주소
 */
QString Config::cameraIp() const
{
    return m_camera["ip"].toString("192.168.0.23");
}

/**
 * @brief 카메라 로그인 ID
 */
QString Config::cameraUsername() const
{
    return m_camera["username"].toString("admin");
}

/**
 * @brief 카메라 로그인 비밀번호
 */
QString Config::cameraPassword() const
{
    return m_camera["password"].toString("");
}

/**
 * @brief RTSP 프로파일 경로
 */
QString Config::cameraProfile() const
{
    return m_camera["profile"].toString("profile2/media.smp");
}

/**
 * @brief RTSP 접속 URL 생성
 */
QString Config::rtspUrl() const
{
    return QString("rtsp://%1:%2@%3/%4")
    .arg(cameraUsername(),
         cameraPassword(),
         cameraIp(),
         cameraProfile());
}

/* =========================
 * Video 관련 설정
 * ========================= */

/**
 * @brief 원본 영상 너비
 */
int Config::sourceWidth() const
{
    return m_video["sourceWidth"].toInt(3840);
}

/**
 * @brief 원본 영상 높이
 */
int Config::sourceHeight() const
{
    return m_video["sourceHeight"].toInt(2160);
}

/**
 * @brief 실제 처리에 사용할 영상 너비
 * (크롭 이후 유효 영역)
 */
int Config::effectiveWidth() const
{
    return m_video["effectiveWidth"].toInt(2880);
}

/**
 * @brief 영상 크롭 시작 X 좌표
 */
int Config::cropOffsetX() const
{
    return m_video["cropOffsetX"].toInt(480);
}

/* =========================
 * OCR 관련 설정
 * ========================= */

/**
 * @brief OCR 언어 설정
 */
QString Config::ocrLanguage() const
{
    return m_ocr["language"].toString("kor");
}

/**
 * @brief Tesseract tessdata 경로
 * - 비어있으면 시스템 기본 경로 사용
 */
QString Config::tessdataPath() const
{
    QString path = m_ocr["tessdataPath"].toString();
    if (path.isEmpty() || path == "null")
    {
        return QString();
    }
    return path;
}

/* =========================
 * Sync 관련 설정
 * ========================= */

/**
 * @brief 기본 동기화 지연 시간 (ms)
 */
int Config::defaultDelayMs() const
{
    return m_sync["defaultDelayMs"].toInt(0);
}
