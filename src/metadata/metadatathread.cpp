#include "metadatathread.h"
#include "util/rtspurl.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

/**
 * @brief MetadataThread 생성자
 * - QThread 기반 메타데이터 수집 스레드
 * - FFmpeg 프로세스를 통해 RTSP 메타데이터 추출
 */
MetadataThread::MetadataThread(QObject *parent)
    : QThread(parent), m_process(nullptr) {}

/**
 * @brief MetadataThread 소멸자
 * - 실행 중인 스레드 및 프로세스를 안전하게 종료
 */
MetadataThread::~MetadataThread() {
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
                                       const QString &profile) {
  // === 멀티스레드 안전을 위한 뮤텍스 보호 ===
  QMutexLocker locker(&m_mutex);

  m_ip = ip;
  m_user = user;
  m_password = password;

  // === RTSP URL 안정화를 위한 profile 정규화 ===
  m_profile = profile;

  // 앞에 '/'가 있으면 제거 (중복 슬래시 방지)
  if (m_profile.startsWith('/')) {
    m_profile.remove(0, 1);
  }

  // profile이 비어있으면 기본값 사용
  if (m_profile.isEmpty()) {
    m_profile = "profile2/media.smp";
  }
}

void MetadataThread::setDisabledTypes(const QSet<QString> &types) {
  QMutexLocker locker(&m_mutex);
  m_disabledTypes = types;
}

/**
 * @brief 메타데이터 스레드 종료 요청
 * - 이벤트 루프 종료 → run() 정리 단계로 이동
 */
void MetadataThread::stop() {
  // 이벤트 루프 종료 요청만 수행한다.
  // 실제 대기는 상위 호출자(CameraManager)에서 타임아웃 정책으로 처리한다.
  requestInterruption();
  quit();
}

/**
 * @brief 메타데이터 수집 메인 루프
 * - FFmpeg 프로세스 실행
 * - 표준 출력으로 들어오는 메타데이터 처리
 */
void MetadataThread::run() {

  // === 내부 버퍼 초기화 ===
  m_buffer.clear();

  // === FFmpeg 실행용 QProcess 생성 ===
  m_process = new QProcess();

  /**
   * [중요]
   * Qt::DirectConnection을 사용하여
   * readyRead 시그널이 "이 스레드"에서 실행되도록 함
   *
   * → 메인 스레드와의 경합 없이 m_process 접근 가능
   */
  connect(m_process, &QProcess::readyReadStandardOutput, this,
          &MetadataThread::onReadyReadStandardOutput, Qt::DirectConnection);

  // === FFmpeg 표준 에러 출력 처리 (디버그 용도) ===
  connect(m_process, &QProcess::readyReadStandardError, this, [=]() {
    // 필요 시 에러 로그 처리 가능
    // QByteArray err = m_process->readAllStandardError();
  });

  // === FFmpeg 실행 파일 경로 탐색 ===
  QString program = findFFmpegPath();
  if (program.isEmpty()) {
    emit logMessage("FFmpeg not found! Please install FFmpeg and add to PATH "
                    "or set FFMPEG_PATH environment variable.");
    delete m_process;
    m_process = nullptr;
    return;
  }

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

  // === RTSP URL 구성 ===
  const QString url = buildRtspUrl(ip, user, password, profile);

  emit logMessage("Starting FFmpeg metadata extraction...");
  emit logMessage(QString("Metadata RTSP URL: %1").arg(maskedRtspUrl(url)));

  // === FFmpeg 실행 인자 구성 ===
  QStringList args;
  args << "-rtsp_transport" << "tcp"
       << "-i" << url << "-map" << "0:d" // metadata stream만 선택
       << "-f" << "data"
       << "-";

  // === FFmpeg 실행 ===
  m_process->start(program, args);

  // === 실행 실패 처리 ===
  if (!m_process->waitForStarted()) {
    emit logMessage("Failed to start FFmpeg: " + m_process->errorString());
    delete m_process;
    m_process = nullptr;
    return;
  }

  /**
   * === 이벤트 루프 시작 ===
   * quit() 호출 전까지 블로킹
   * readyRead 시그널이 여기서 처리됨
   */
  exec();

  // === 이벤트 루프 종료 후 프로세스 정리 ===
  if (m_process) {
    if (m_process->state() != QProcess::NotRunning) {
      m_process->terminate();
      if (!m_process->waitForFinished(500)) {
        m_process->kill();
      }
    }
    delete m_process;
    m_process = nullptr;
  }
}

/**
 * @brief FFmpeg 표준 출력 데이터 수신 슬롯
 * - Worker Thread에서 실행됨
 */
void MetadataThread::onReadyReadStandardOutput() {

  if (isInterruptionRequested()) {
    return;
  }

  if (!m_process)
    return;

  // === 새 데이터 읽기 ===
  QByteArray newData = m_process->readAllStandardOutput();
  m_buffer.append(newData);

  // === 버퍼 처리 ===
  processBuffer();
}

/**
 * @brief 메타데이터 버퍼 처리
 * - XML 프레임 단위로 잘라서 파싱
 */
void MetadataThread::processBuffer() {

  if (isInterruptionRequested()) {
    return;
  }

  // === 버퍼 폭주 방지 안전장치 ===
  if (m_buffer.size() > 4 * 1024 * 1024) {
    emit logMessage("Buffer too large, clearing...");
    m_buffer.clear();
    return;
  }

  while (true) {
    if (isInterruptionRequested()) {
      return;
    }

    // === MetadataStream 시작 태그 탐색 ===
    int startTagIndex = m_buffer.indexOf("<tt:MetadataStream");
    if (startTagIndex == -1) {

      // 시작 태그를 오래 못 찾으면 버퍼 정리
      if (m_buffer.size() > 1024 * 1024) {
        m_buffer.clear();
      }
      break;
    }

    // === 시작 태그 이전 쓰레기 데이터 제거 ===
    if (startTagIndex > 0) {
      m_buffer.remove(0, startTagIndex);
    }

    // === Frame 종료 태그 탐색 ===
    int endTagIndex = m_buffer.indexOf("</tt:Frame>");
    if (endTagIndex == -1) {
      // 프레임이 아직 완성되지 않음
      break;
    }

    // === 완전한 프레임 데이터 추출 ===
    int frameLength = endTagIndex + QString("</tt:Frame>").length();
    QString frameData = QString::fromUtf8(m_buffer.left(frameLength));

    // === 프레임 파싱 ===
    parseFrame(frameData);

    // === 처리 완료한 데이터 제거 ===
    m_buffer.remove(0, frameLength);
  }
}

/**
 * @brief 단일 메타데이터 프레임 파싱
 * @param frameXml XML 문자열
 */
void MetadataThread::parseFrame(const QString &frameXml) {

  if (isInterruptionRequested()) {
    return;
  }

  QList<ObjectInfo> objects;

  // === 객체 단위 정규식 파싱 ===
  QRegularExpression objectRe(
      "<tt:Object ObjectId=\"(\\d+)\">(.*?)</tt:Object>",
      QRegularExpression::DotMatchesEverythingOption);

  QRegularExpressionMatchIterator i = objectRe.globalMatch(frameXml);

  while (i.hasNext()) {
    if (isInterruptionRequested()) {
      return;
    }
    QRegularExpressionMatch match = i.next();

    ObjectInfo info;
    info.id = match.captured(1).toInt();
    QString objectContent = match.captured(2);

    // === Bounding Box 파싱 ===
    QRegularExpression bboxRe(
        "<tt:BoundingBox\\s+left=\"([\\d\\.]+)\"\\s+top=\"([\\d\\.]+)\"\\s+"
        "right=\"([\\d\\.]+)\"\\s+bottom=\"([\\d\\.]+)\"");

    QRegularExpressionMatch bboxMatch = bboxRe.match(objectContent);
    if (!bboxMatch.hasMatch())
      continue;

    float left = bboxMatch.captured(1).toFloat();
    float top = bboxMatch.captured(2).toFloat();
    float right = bboxMatch.captured(3).toFloat();
    float bottom = bboxMatch.captured(4).toFloat();

    info.rect = QRectF(left, top, right - left, bottom - top);

    // === 객체 타입 파싱 ===
    QRegularExpression typeRe("<tt:Type[^>]*>([^<]+)</tt:Type>");
    QRegularExpressionMatch typeMatch = typeRe.match(objectContent);
    info.type = typeMatch.hasMatch() ? typeMatch.captured(1) : "Unknown";

    // === 비활성화된 객체 타입 필터링 ===
    // UI 체크박스에서 체크 해제된 타입은 건너뜁니다.
    {
      QMutexLocker locker(&m_mutex);
      if (m_disabledTypes.contains(info.type)) {
        continue;
      }
    }

    // === 신뢰도(Score) 파싱 ===
    // Likelihood="0.95" 또는 Confidence="0.95" 등 검색
    QRegularExpression scoreRe("(Likelihood|confidence|Score)=\"([\\d\\.]+)\"");
    QRegularExpressionMatch scoreMatch = scoreRe.match(objectContent);
    if (scoreMatch.hasMatch()) {
      info.score = scoreMatch.captured(2).toFloat();
    } else {
      info.score = 0.0f;
    }

    // === 번호판 정보 파싱 (다양한 태그 대응) ===
    info.plate = "";
    // 1. <tt:PlateNumber>상세</tt:PlateNumber>
    // 2. <tt:Plate>상세</tt:Plate>
    // 3. 속성 Plate="상세"
    QRegularExpression plateRe(
        "<tt:(PlateNumber|Plate)[^>]*>([^<]+)</tt:(PlateNumber|Plate)>");
    QRegularExpressionMatch plateMatch = plateRe.match(objectContent);
    if (plateMatch.hasMatch()) {
      info.extraInfo = plateMatch.captured(2);
      info.plate = info.extraInfo;
    } else {
      // 속성형태 체크: Plate="123가4567"
      QRegularExpression plateAttrRe("Plate=\"([^\"]+)\"");
      QRegularExpressionMatch plateAttrMatch = plateAttrRe.match(objectContent);
      if (plateAttrMatch.hasMatch()) {
        info.plate = plateAttrMatch.captured(1);
        info.extraInfo = info.plate;
      }
    }

    if (!info.plate.isEmpty()) {
      emit logMessage(QString("Plate Detected! ID: %1, Num: %2")
                          .arg(info.id)
                          .arg(info.plate));
    }

    objects.append(info);
  }

  // Always propagate the frame state, including empty-object frames.
  // Otherwise the last non-empty metadata may stay on screen as a ghost box.
  emit metadataReceived(objects);
}

/**
 * @brief FFmpeg 실행 경로 탐색
 * @return ffmpeg 실행 파일 경로 (없으면 빈 문자열)
 */
QString MetadataThread::findFFmpegPath() {

  // === 1순위: FFMPEG_PATH 환경 변수 ===
  QString envPath = qEnvironmentVariable("FFMPEG_PATH");
  if (!envPath.isEmpty()) {
    QFileInfo fi(envPath);
    if (fi.exists() && fi.isExecutable()) {
      return envPath;
    }

    // 디렉토리인 경우 ffmpeg.exe 추가
    QString exePath = envPath + "/ffmpeg.exe";
    if (QFileInfo::exists(exePath)) {
      return exePath;
    }
  }

  // === 2순위: 시스템 PATH 검색 ===
  QString found = QStandardPaths::findExecutable("ffmpeg");
  if (!found.isEmpty()) {
    return found;
  }

  // === 찾을 수 없음 ===
  return QString();
}
