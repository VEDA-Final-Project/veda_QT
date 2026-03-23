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
}

// 현재 시각(nowMs)을 기준으로
// "지연 시간이 지난 메타데이터"만 꺼내서 반환
QList<ObjectInfo> MetadataSynchronizer::consumeReady(qint64 nowMs)
{
    // 큐에 데이터가 있는 동안 반복
    while (!m_queue.isEmpty())
    {
        // 큐의 맨 앞 데이터가
        // (타임스탬프 + 지연시간) <= 현재시간 이면
        if (m_queue.first().first + m_delayMs <= nowMs)
        {
            // 해당 메타데이터를 현재 유효한 데이터로 갱신
            m_currentObjects = m_queue.takeFirst().second;
        }
        else
        {
            // 아직 시간이 안 됐으면 멈춤
            break;
        }
    }

    // 가장 최근에 유효해진 객체 정보 반환
    return m_currentObjects;
}
