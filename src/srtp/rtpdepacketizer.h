#ifndef RTPDEPACKETIZER_H
#define RTPDEPACKETIZER_H

#include <QObject>
#include <QByteArray>
#include <QMap>

/**
 * @brief RTP 패킷 재조립 클래스 (Step 4)
 * - 복호화된 RTP 페이로드에서 NAL Unit(영상) 및 XML(메타데이터)을 재조립합니다.
 * - H.264/H.265 FU-A(Fragmentation Unit) 처리를 포함합니다.
 */
class RtpDepacketizer : public QObject {
  Q_OBJECT
public:
  explicit RtpDepacketizer(QObject *parent = nullptr);
  void setH264ParameterSets(const QList<QByteArray> &parameterSets);

  /**
   * @brief RTP 패킷 투입
   * @param rtpPacket 복호화된 RTP 전체 패킷 (헤더 포함)
   */
  void processPacket(const QByteArray &rtpPacket);

signals:
  void frameReady(const QByteArray &nalUnit, uint32_t timestamp);
  void metadataReady(const QByteArray &xmlData, uint32_t timestamp);

private:
  void handleVideoPacket(const QByteArray &payload,
                         uint32_t timestamp,
                         quint16 seqNum,
                         bool markerBit);
  void handleMetadataPacket(const QByteArray &payload, uint32_t timestamp);
  void appendAnnexBNal(const QByteArray &nalPayload);
  void flushVideoAccessUnit(uint32_t timestamp);
  void discardVideoAccessUnit(const char *reason, uint32_t timestamp);
  void handleH264StapA(const QByteArray &payload);
  bool accessUnitContainsNalType(quint8 nalType) const;
  void prependParameterSetsIfNeeded(bool force);

  QByteArray m_videoBuffer;      // Access unit 재조립 버퍼
  QByteArray m_fuBuffer;         // FU-A 단일 NAL 재조립 버퍼
  QByteArray m_metadataBuffer;   // 메타데이터 재조립 버퍼
  QList<QByteArray> m_h264ParameterSets;
  uint32_t m_lastVideoTs = 0;
  uint32_t m_lastMetaTs = 0;
  quint16 m_lastVideoSeq = 0;
  bool m_haveLastVideoSeq = false;
  bool m_videoAccessUnitDamaged = false;
  bool m_waitingForIdr = true;
  bool m_forceParameterSetsOnNextIdr = true;
};

#endif // RTPDEPACKETIZER_H
