#include "metadatasynchronizer.h"
#include <QtGlobal>   // qMax 같은 Qt 유틸 함수 사용

// 외부에서 지연 시간(ms)을 설정하는 함수
void MetadataSynchronizer::setDelayMs(int delayMs)
{
    // delayMs가 음수로 들어와도 0으로 보정
    m_delayMs = qMax(0, delayMs);
}

// 새로운 메타데이터(ObjectInfo 목록)를 큐에 저장
// tsMs: 이 메타데이터가 생성된 시각(ms)
void MetadataSynchronizer::pushMetadata(const QList<ObjectInfo> &objects, qint64 tsMs)
{
    // (타임스탬프, 객체 리스트) 형태로 큐에 추가
    m_queue.append(qMakePair(tsMs, objects));

    // 소비가 잠시 멈춰도 큐가 무한히 커지지 않도록 오래된 항목부터 버립니다.
    while (m_queue.size() > kMaxQueuedMetadataFrames)
    {
        m_queue.removeFirst();
    }
}

// 프레임 타임라인(frameTimestampMs)을 기준으로
// "지연 시간이 지난 메타데이터"만 꺼내서 반환
QList<ObjectInfo> MetadataSynchronizer::consumeReady(qint64 frameTimestampMs)
{
    // 큐에 데이터가 있는 동안 반복
    while (!m_queue.isEmpty())
    {
        // 큐의 맨 앞 데이터가
        // (타임스탬프 + 지연시간) <= 현재 프레임 시각 이면
        if (m_queue.first().first + m_delayMs <= frameTimestampMs)
        {
            // 해당 메타데이터를 현재 유효한 데이터로 갱신
            const auto readyEntry = m_queue.takeFirst();
            m_currentTimestampMs = readyEntry.first;
            m_currentObjects = readyEntry.second;
        }
        else
        {
            // 아직 시간이 안 됐으면 멈춤
            break;
        }
    }

    // 마지막 metadata가 너무 오래되면 overlay를 비워 ghost box를 막습니다.
    if (m_currentTimestampMs >= 0 &&
        frameTimestampMs > (m_currentTimestampMs + m_delayMs + kMetadataStaleTimeoutMs))
    {
        m_currentObjects.clear();
        m_currentTimestampMs = -1;
    }

    // 가장 최근에 유효해진 객체 정보 반환
    return m_currentObjects;
}
