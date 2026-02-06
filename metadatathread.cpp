
#include "metadatathread.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QRegularExpression>

MetadataThread::MetadataThread(QObject *parent)
    : QThread(parent), m_process(nullptr) {}

MetadataThread::~MetadataThread() {
  stop();
  wait();
}

void MetadataThread::setConnectionInfo(const QString &ip, const QString &user,
                                       const QString &password) {
  QMutexLocker locker(&m_mutex);
  m_ip = ip;
  m_user = user;
  m_password = password;
}

void MetadataThread::stop() {
  // 이벤트 루프 종료 요청
  quit();
  wait();
}

void MetadataThread::run() {
  m_buffer.clear();

  m_process = new QProcess();

  // [핵심] Qt::DirectConnection을 사용하여 슬롯이 이 스레드(Worker Thread)에서
  // 실행되도록 함 이렇게 해야 메인 스레드와의 경합 없이 안전하게 m_process에
  // 접근 가능
  connect(m_process, &QProcess::readyReadStandardOutput, this,
          &MetadataThread::onReadyReadStandardOutput, Qt::DirectConnection);

  // 에러 출력 로그
  connect(m_process, &QProcess::readyReadStandardError, this, [=]() {
    // 에러 로그는 Blocking 없이 간단히 읽기
    // QByteArray err = m_process->readAllStandardError();
  });

  QString program = "D:/ffmpeg-8.0.1-essentials_build/bin/ffmpeg.exe";
  QString url = QString("rtsp://%1:%2@%3/profile2/media.smp")
                    .arg(m_user, m_password, m_ip);

  emit logMessage("Starting FFmpeg metadata extraction...");

  QStringList args;
  args << "-rtsp_transport" << "tcp"
       << "-i" << url << "-map" << "0:d"
       << "-f" << "data"
       << "-";

  m_process->start(program, args);

  if (!m_process->waitForStarted()) {
    emit logMessage("Failed to start FFmpeg: " + m_process->errorString());
    delete m_process;
    m_process = nullptr;
    return;
  }

  // Qt 이벤트 루프 실행 (여기서 블로킹하며 시그널 처리)
  exec();

  // 루프가 끝나면(quit 호출 시) 정리 작업 수행
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

void MetadataThread::onReadyReadStandardOutput() {
  // exec() 루프 내에서 호출됨 -> Worker Thread에서 실행됨 (Safe)
  if (!m_process)
    return;

  QByteArray newData = m_process->readAllStandardOutput();
  m_buffer.append(newData);
  processBuffer();
}

void MetadataThread::processBuffer() {
  // 버퍼가 너무 커지면 강제로 정리 (안전장치)
  if (m_buffer.size() > 4 * 1024 * 1024) {
    emit logMessage("Buffer too large, clearing...");
    m_buffer.clear();
    return;
  }

  while (true) {
    int startTagIndex = m_buffer.indexOf("<tt:MetadataStream");
    if (startTagIndex == -1) {
      // 시작 태그가 없으면, 혹시 모를 쓰레기 데이터 정리 (선택사항, 여기서는
      // 생략하거나 일부 정리) 다만 스트림 중간부터 시작할 수도 있으니 주의.
      // 여기서는 단순하게 유지
      if (m_buffer.size() > 1024 * 1024) { // 1MB 넘도록 시작 태그 못찾으면
        m_buffer.clear();
      }
      break;
    }

    // 시작 태그 이전 데이터는 버림
    if (startTagIndex > 0) {
      m_buffer.remove(0, startTagIndex);
    }

    // 끝 태그 찾기 (간단히 Frame 끝을 기준으로 함, 실제 구조에 따라 변경 필요)
    // 기존 코드는 </tt:Frame>을 기준으로 잘랐음.
    int endTagIndex = m_buffer.indexOf("</tt:Frame>");
    if (endTagIndex == -1) {
      // 끝 태그가 아직 안들어옴 -> 데이터 더 기다림
      break;
    }

    // 하나의 온전한 프레임 데이터 추출
    int frameLength = endTagIndex + QString("</tt:Frame>").length();
    QString frameData = QString::fromUtf8(m_buffer.left(frameLength));

    // 처리
    parseFrame(frameData);

    // 버퍼에서 제거
    m_buffer.remove(0, frameLength);
  }
}

void MetadataThread::parseFrame(const QString &frameXml) {
  QList<ObjectInfo> objects;

  // 객체 목록 정규식
  QRegularExpression objectRe(
      "<tt:Object ObjectId=\"(\\d+)\">(.*?)</tt:Object>",
      QRegularExpression::DotMatchesEverythingOption);
  QRegularExpressionMatchIterator i = objectRe.globalMatch(frameXml);

  while (i.hasNext()) {
    QRegularExpressionMatch match = i.next();
    QString objectContent = match.captured(2);

    ObjectInfo info;
    info.id = match.captured(1).toInt();

    // Bounding Box 파싱
    QRegularExpression bboxRe(
        "<tt:BoundingBox\\s+left=\"([\\d\\.]+)\"\\s+top=\"([\\d\\.]+)\"\\s+"
        "right=\"([\\d\\.]+)\"\\s+bottom=\"([\\d\\.]+)\"");
    QRegularExpressionMatch bboxMatch = bboxRe.match(objectContent);

    if (bboxMatch.hasMatch()) {
      float left = bboxMatch.captured(1).toFloat();
      float top = bboxMatch.captured(2).toFloat();
      float right = bboxMatch.captured(3).toFloat();
      float bottom = bboxMatch.captured(4).toFloat();
      info.rect = QRect(left, top, right - left, bottom - top);
    } else {
      continue; // 좌표 없으면 스킵
    }

    // 타입 파싱
    QRegularExpression typeRe("<tt:Type[^>]*>([^<]+)</tt:Type>");
    QRegularExpressionMatch typeMatch = typeRe.match(objectContent);
    if (typeMatch.hasMatch()) {
      info.type = typeMatch.captured(1);
    } else {
      info.type = "Unknown";
    }

    // 번호판 등 추가 정보
    QRegularExpression plateRe("<tt:PlateNumber[^>]*>([^<]+)</tt:PlateNumber>");
    QRegularExpressionMatch plateMatch = plateRe.match(objectContent);
    if (plateMatch.hasMatch()) {
      info.extraInfo = plateMatch.captured(1);
      QString msg = QString("Plate Detected! ID: %1, Num: %2")
                        .arg(info.id)
                        .arg(info.extraInfo);
      emit logMessage(msg);
    }

    objects.append(info);
  }

  if (!objects.isEmpty()) {
    emit metadataReceived(objects);
  }
}
