
#include "metadatathread.h"
#include <QEventLoop>
#include <QRegularExpression>
#include <QDebug>
#include <QCoreApplication>

MetadataThread::MetadataThread(QObject *parent)
    : QThread(parent), m_stop(false), m_process(nullptr)
{
}

MetadataThread::~MetadataThread()
{
    stop();
    wait();
}

void MetadataThread::setConnectionInfo(const QString &ip, const QString &user, const QString &password)
{
    QMutexLocker locker(&m_mutex);
    m_ip = ip;
    m_user = user;
    m_password = password;
}

void MetadataThread::stop()
{
    // 이벤트 루프 종료 요청
    quit();
    wait();
}

void MetadataThread::run()
{
    m_stop = false;
    m_buffer.clear();

    m_process = new QProcess();

    // [핵심] Qt::DirectConnection을 사용하여 슬롯이 이 스레드(Worker Thread)에서 실행되도록 함
    // 이렇게 해야 메인 스레드와의 경합 없이 안전하게 m_process에 접근 가능
    connect(m_process, &QProcess::readyReadStandardOutput, this, &MetadataThread::onReadyReadStandardOutput, Qt::DirectConnection);

    // 에러 출력 로그
    connect(m_process, &QProcess::readyReadStandardError, this, [=](){
        // 에러 로그는 Blocking 없이 간단히 읽기
        // QByteArray err = m_process->readAllStandardError();
    });

    QString program = "C:/ffmpeg-2026-02-04-git-627da1111c-full_build/bin/ffmpeg.exe";
    QString url = QString("rtsp://%1:%2@%3/profile2/media.smp").arg(m_user, m_password, m_ip);

    emit logMessage("Starting FFmpeg metadata extraction...");

    QStringList args;
    args << "-rtsp_transport" << "tcp"
         << "-i" << url
         << "-map" << "0:d"
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

void MetadataThread::onReadyReadStandardOutput()
{
    // exec() 루프 내에서 호출됨 -> Worker Thread에서 실행됨 (Safe)
    if (!m_process) return;

    QByteArray newData = m_process->readAllStandardOutput();
    m_buffer.append(newData);

    if (m_buffer.size() > 2 * 1024 * 1024) m_buffer.clear();

    QString strData(m_buffer);
    QList<ObjectInfo> objects;

    int lastTagIndex = strData.lastIndexOf("</tt:Frame>");
    if (lastTagIndex != -1) {
        QString processData = strData.left(lastTagIndex + 11);
        m_buffer = strData.mid(lastTagIndex + 11).toUtf8();

        QRegularExpression objectRe("<tt:Object ObjectId=\"(\\d+)\">(.*?)</tt:Object>", QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatchIterator i = objectRe.globalMatch(processData);

        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            QString objectContent = match.captured(2);

            ObjectInfo info;
            info.id = match.captured(1).toInt();

            QRegularExpression bboxRe("<tt:BoundingBox\\s+left=\"([\\d\\.]+)\"\\s+top=\"([\\d\\.]+)\"\\s+right=\"([\\d\\.]+)\"\\s+bottom=\"([\\d\\.]+)\"");
            QRegularExpressionMatch bboxMatch = bboxRe.match(objectContent);

            if (bboxMatch.hasMatch()) {
                float left = bboxMatch.captured(1).toFloat();
                float top = bboxMatch.captured(2).toFloat();
                float right = bboxMatch.captured(3).toFloat();
                float bottom = bboxMatch.captured(4).toFloat();
                info.rect = QRect(left, top, right - left, bottom - top);
            } else {
                continue;
            }

            QRegularExpression typeRe("<tt:Type[^>]*>([^<]+)</tt:Type>");
            QRegularExpressionMatch typeMatch = typeRe.match(objectContent);
            if (typeMatch.hasMatch()) {
                info.type = typeMatch.captured(1);
            } else {
                info.type = "Unknown";
            }

            QRegularExpression plateRe("<tt:PlateNumber[^>]*>([^<]+)</tt:PlateNumber>");
            QRegularExpressionMatch plateMatch = plateRe.match(objectContent);
            if (plateMatch.hasMatch()) {
                info.extraInfo = plateMatch.captured(1);
                QString msg = QString("Plate Detected! ID: %1, Num: %2").arg(info.id).arg(info.extraInfo);
                emit logMessage(msg);
            }

            objects.append(info);
        }

        if (!objects.isEmpty()) {
            emit metadataReceived(objects);
        }
    }
}


