#include "rtpdepacketizer.h"
#include <QDebug>
#include <QtEndian>

RtpDepacketizer::RtpDepacketizer(QObject *parent) : QObject(parent) {}

void RtpDepacketizer::setH264ParameterSets(const QList<QByteArray> &parameterSets) {
  m_h264ParameterSets.clear();
  for (const QByteArray &nal : parameterSets) {
    if (!nal.isEmpty()) {
      m_h264ParameterSets.push_back(nal);
    }
  }
}

void RtpDepacketizer::processPacket(const QByteArray &rtpPacket) {
  if (rtpPacket.size() < 12) return; // 최소 RTP 헤더 크기

  // RTP 헤더 파싱 (RFC 3550 Section 5.1)
  uint8_t v_p_x_cc = static_cast<uint8_t>(rtpPacket[0]);
  const uint8_t markerPayload = static_cast<uint8_t>(rtpPacket[1]);
  uint8_t payloadType = markerPayload & 0x7F;
  const bool markerBit = (markerPayload & 0x80) != 0;
  const quint16 seqNum =
      qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(rtpPacket.data() + 2));
  uint32_t timestamp = qFromBigEndian<uint32_t>(reinterpret_cast<const uchar*>(rtpPacket.data() + 4));
  // uint32_t ssrc = qFromBigEndian<uint32_t>(reinterpret_cast<const uchar*>(rtpPacket.data() + 8));
  int packetSize = rtpPacket.size();

  const bool hasPadding = (v_p_x_cc & 0x20) != 0;
  if (hasPadding) {
    const int paddingLen = static_cast<uint8_t>(rtpPacket.back());
    if (paddingLen <= 0 || paddingLen > packetSize - 12) {
      qWarning() << "[SRTP][Step4] Invalid RTP padding length:" << paddingLen;
      return;
    }
    packetSize -= paddingLen;
  }

  // CSRC 리스트 건너뛰기
  int cc = v_p_x_cc & 0x0F;
  int payloadOffset = 12 + (cc * 4);
  
  // RTP 확장 헤더 처리
  if (v_p_x_cc & 0x10) {
    if (packetSize < payloadOffset + 4) return;
    int extLen = qFromBigEndian<uint16_t>(reinterpret_cast<const uchar*>(rtpPacket.data() + payloadOffset + 2));
    payloadOffset += 4 + (extLen * 4);
  }

  if (packetSize <= payloadOffset) return;
  QByteArray payload = rtpPacket.mid(payloadOffset, packetSize - payloadOffset);

  // Payload Type에 따른 분류 (SDP 정보를 미리 알 수 없으므로 일반적인 동적 할당 영역 96+ 사용)
  // 여기서는 영상(H.264/H.265)과 메타데이터(XML)를 구분하는 로직이 필요함
  // 한화비전의 경우 보통 영상은 96, 메타데이터는 100~105 사이인 경우가 많음
  
  if (payloadType == 96 || payloadType == 97 || payloadType == 98) {
    handleVideoPacket(payload, timestamp, seqNum, markerBit);
  } else if (payloadType == 107) {
    handleMetadataPacket(payload, timestamp);
  } else {
    // 텍스트 기반(XML)인지 확인하여 메타데이터로 처리
    if (payload.startsWith("<?xml") || payload.contains("<tt:MetadataStream")) {
        handleMetadataPacket(payload, timestamp);
    } else {
        // 버퍼링 중인 데이터가 있다면 메타데이터 조각일 가능성 상존
        if (!m_metadataBuffer.isEmpty()) {
            handleMetadataPacket(payload, timestamp);
        }
    }
  }
}

void RtpDepacketizer::handleVideoPacket(const QByteArray &payload,
                                        uint32_t timestamp,
                                        quint16 seqNum,
                                        bool markerBit) {
  if (payload.isEmpty()) return;

  if (m_lastVideoTs != 0 && timestamp != m_lastVideoTs) {
    flushVideoAccessUnit(m_lastVideoTs);
    m_fuBuffer.clear();
  }

  if (m_haveLastVideoSeq) {
    const quint16 expectedSeq = static_cast<quint16>(m_lastVideoSeq + 1);
    if (seqNum != expectedSeq) {
      qWarning() << "[SRTP][RTP] Video packet loss/reorder detected. expectedSeq:"
                 << expectedSeq << "actualSeq:" << seqNum << "timestamp:" << timestamp;
      m_videoAccessUnitDamaged = true;
      m_forceParameterSetsOnNextIdr = true;
      m_fuBuffer.clear();
    }
  }
  m_lastVideoSeq = seqNum;
  m_haveLastVideoSeq = true;
  m_lastVideoTs = timestamp;

  // H.264 FU-A (Fragmentation Unit) 처리 (RFC 6184 Section 5.8)
  uint8_t nri_type = static_cast<uint8_t>(payload[0]);
  uint8_t type = nri_type & 0x1F;
  if (type == 24) { // STAP-A
    handleH264StapA(payload);
  } else if (type == 28) { // FU-A
    if (payload.size() < 2) return;
    uint8_t fuHeader = static_cast<uint8_t>(payload[1]);
    bool startBit = fuHeader & 0x80;
    bool endBit = fuHeader & 0x40;
    uint8_t nalType = fuHeader & 0x1F;

    if (startBit) {
      m_fuBuffer.clear();
      m_fuBuffer.append('\x00');
      m_fuBuffer.append('\x00');
      m_fuBuffer.append('\x00');
      m_fuBuffer.append('\x01');
      m_fuBuffer.append((nri_type & 0xE0) | nalType);
    }

    if (m_fuBuffer.isEmpty()) {
      return;
    }

    m_fuBuffer.append(payload.mid(2));

    if (endBit) {
      m_videoBuffer.append(m_fuBuffer);
      m_fuBuffer.clear();
    }
  } else if (type >= 1 && type <= 23) {
    appendAnnexBNal(payload);
  } else {
    qWarning() << "[SRTP][Step4] Unsupported H.264 RTP packetization type:" << type;
  }

  if (markerBit) {
    flushVideoAccessUnit(timestamp);
  }
}

void RtpDepacketizer::appendAnnexBNal(const QByteArray &nalPayload) {
  if (nalPayload.isEmpty()) return;

  m_videoBuffer.append("\x00\x00\x00\x01", 4);
  m_videoBuffer.append(nalPayload);
}

void RtpDepacketizer::flushVideoAccessUnit(uint32_t timestamp) {
  if (m_videoBuffer.isEmpty()) {
    return;
  }
  if (m_videoAccessUnitDamaged) {
    discardVideoAccessUnit("damaged_access_unit", timestamp);
    return;
  }

  const bool hasIdr = accessUnitContainsNalType(5);
  const bool hasSps = accessUnitContainsNalType(7);
  const bool hasPps = accessUnitContainsNalType(8);

  if (m_waitingForIdr && !hasIdr) {
    discardVideoAccessUnit("waiting_for_idr", timestamp);
    return;
  }

  if (hasIdr) {
    prependParameterSetsIfNeeded(m_forceParameterSetsOnNextIdr || !hasSps || !hasPps);
    m_waitingForIdr = false;
    m_forceParameterSetsOnNextIdr = false;
  }

  m_videoAccessUnitDamaged = false;
  emit frameReady(m_videoBuffer, timestamp);
  m_videoBuffer.clear();
}

void RtpDepacketizer::discardVideoAccessUnit(const char *reason, uint32_t timestamp) {
  qWarning() << "[SRTP][RTP] Dropping video access unit:" << reason
             << "timestamp:" << timestamp << "size:" << m_videoBuffer.size();
  m_videoBuffer.clear();
  m_fuBuffer.clear();
  m_videoAccessUnitDamaged = false;
  if (qstrcmp(reason, "damaged_access_unit") == 0) {
    m_forceParameterSetsOnNextIdr = true;
  }
}

void RtpDepacketizer::handleH264StapA(const QByteArray &payload) {
  if (payload.size() < 3) return;

  int offset = 1; // Skip STAP-A indicator
  while (offset + 2 <= payload.size()) {
    const quint16 nalSize = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar *>(payload.constData() + offset));
    offset += 2;
    if (nalSize == 0 || offset + nalSize > payload.size()) {
      qWarning() << "[SRTP][Step4] Invalid STAP-A NAL size:" << nalSize
                 << "payloadSize:" << payload.size() << "offset:" << offset;
      return;
    }
    appendAnnexBNal(payload.mid(offset, nalSize));
    offset += nalSize;
  }
}

bool RtpDepacketizer::accessUnitContainsNalType(quint8 nalType) const {
  const QByteArray startCode("\x00\x00\x00\x01", 4);
  int pos = 0;
  while ((pos = m_videoBuffer.indexOf(startCode, pos)) != -1) {
    const int nalHeaderPos = pos + 4;
    if (nalHeaderPos < m_videoBuffer.size()) {
      const quint8 currentNalType =
          static_cast<quint8>(m_videoBuffer[nalHeaderPos]) & 0x1F;
      if (currentNalType == nalType) {
        return true;
      }
    }
    pos = nalHeaderPos;
  }
  return false;
}


void RtpDepacketizer::prependParameterSetsIfNeeded(bool force) {
  if (m_h264ParameterSets.isEmpty() || m_videoBuffer.isEmpty()) {
    return;
  }

  const bool hasSps = accessUnitContainsNalType(7);
  const bool hasPps = accessUnitContainsNalType(8);
  if (!force && hasSps && hasPps) {
    return;
  }

  QByteArray prefix;
  for (const QByteArray &nal : m_h264ParameterSets) {
    prefix.append("\x00\x00\x00\x01", 4);
    prefix.append(nal);
  }
  m_videoBuffer.prepend(prefix);
}

void RtpDepacketizer::handleMetadataPacket(const QByteArray &payload, uint32_t timestamp) {
  // XML 데이터는 보통 프레임 경계가 명확하지 않으므로 타임스탬프가 바뀌면 이전 것을 완성으로 간주하거나,
  // 닫는 태그를 확인하는 로직이 필요함
  if (m_lastMetaTs != 0 && m_lastMetaTs != timestamp) {
    if (!m_metadataBuffer.isEmpty()) {
      emit metadataReady(m_metadataBuffer, m_lastMetaTs);
      m_metadataBuffer.clear();
    }
  }

  m_metadataBuffer.append(payload);
  m_lastMetaTs = timestamp;

  // XML 프레임 종료 태그 확인
  if (m_metadataBuffer.endsWith("</tt:Frame>") || m_metadataBuffer.contains("</tt:MetadataStream>")) {
    emit metadataReady(m_metadataBuffer, timestamp);
    m_metadataBuffer.clear();
  }
}
