#include "cameramanager.h"

/**
 * @brief CameraManager 생성자
 * - 비디오 스레드, 메타데이터 스레드 생성
 * - 내부 스레드의 시그널을 외부(CameraManager)로 전달
 */
CameraManager::CameraManager(QObject *parent) : QObject(parent) {

    // === 스레드 생성 (QObject 부모 설정으로 메모리 관리) ===
    m_videoThread = new VideoThread(this);
    m_metadataThread = new MetadataThread(this);

    // === VideoThread → CameraManager 시그널 전달 ===
    // 캡처된 프레임을 그대로 외부(UI 등)로 전달
    connect(m_videoThread, &VideoThread::frameCaptured,
            this, &CameraManager::frameCaptured);

    // === MetadataThread → CameraManager 시그널 전달 ===
    // 객체 메타데이터 전달
    connect(m_metadataThread, &MetadataThread::metadataReceived,
            this, &CameraManager::metadataReceived);

    // 로그 메시지 전달
    connect(m_metadataThread, &MetadataThread::logMessage,
            this, &CameraManager::logMessage);
}

/**
 * @brief CameraManager 소멸자
 * - 실행 중인 스레드가 있다면 안전하게 종료
 */
CameraManager::~CameraManager() {
    stop();
}

/**
 * @brief 카메라 스트림 시작
 * - 설정 로드
 * - RTSP 비디오 스트림 시작
 * - 메타데이터 스트림 시작
 */
void CameraManager::start() {

    // === 이미 실행 중이면 중복 실행 방지 ===
    if (isRunning())
        return;

    // === 설정 파일 재로드 (실행 중 설정 변경 반영) ===
    if (!Config::instance().load()) {
        emit logMessage("Warning: could not reload config; using existing values.");
    }

    // === 설정 값 가져오기 ===
    const auto &cfg = Config::instance();
    QString ip = cfg.cameraIp();
    QString id = cfg.cameraUsername();
    QString pw = cfg.cameraPassword();
    QString profile = cfg.cameraProfile();
    QString url = cfg.rtspUrl();

    // === 시작 로그 출력 ===
    emit logMessage(QString("Starting camera with IP=%1, profile=%2")
                        .arg(ip, profile));
    emit logMessage(QString("RTSP URL: %1").arg(url));

    // === 비디오 스트림 시작 ===
    m_videoThread->setUrl(url);
    m_videoThread->start();

    // === 메타데이터 스트림 시작 ===
    // 비디오와 동일한 profile 사용 → 싱크 유지
    m_metadataThread->setConnectionInfo(ip, id, pw, profile);
    m_metadataThread->start();
}

/**
 * @brief 카메라 재시작
 * - 스트림 완전 종료 후 다시 시작
 */
void CameraManager::restart() {
    stop();
    start();
}

/**
 * @brief 카메라 스트림 종료
 * - 비디오/메타데이터 스레드를 안전하게 종료
 */
void CameraManager::stop() {

    // === 비디오 스레드 종료 ===
    if (m_videoThread->isRunning()) {
        m_videoThread->stop(); // 종료 요청
        m_videoThread->wait(); // 실제 종료까지 대기
    }

    // === 메타데이터 스레드 종료 ===
    if (m_metadataThread->isRunning()) {
        m_metadataThread->stop();
        m_metadataThread->wait();
    }
}

/**
 * @brief 카메라 실행 상태 확인
 * @return 하나라도 실행 중이면 true
 */
bool CameraManager::isRunning() const {
    return m_videoThread->isRunning()
    || m_metadataThread->isRunning();
}
