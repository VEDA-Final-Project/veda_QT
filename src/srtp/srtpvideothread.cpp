#include "srtpvideothread.h"
#include <QDebug>
#include <QDateTime>

namespace {
AVPixelFormat normalizeDeprecatedPixelFormat(AVPixelFormat pixFmt, int *srcRange) {
  *srcRange = 0;
  switch (pixFmt) {
    case AV_PIX_FMT_YUVJ420P:
      *srcRange = 1;
      return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P:
      *srcRange = 1;
      return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P:
      *srcRange = 1;
      return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P:
      *srcRange = 1;
      return AV_PIX_FMT_YUV440P;
    default:
      return pixFmt;
  }
}
} // namespace

SrtpVideoThread::SrtpVideoThread(QObject *parent)
    : QObject(parent),
      m_codecCtx(nullptr),
      m_parserCtx(nullptr),
      m_frame(nullptr),
      m_packet(nullptr),
      m_swsCtx(nullptr),
      m_initialized(false) {
}

SrtpVideoThread::~SrtpVideoThread() {
  cleanup();
}

bool SrtpVideoThread::initCodec(AVCodecID codecId) {
  QMutexLocker locker(&m_mutex);

  if (m_initialized || m_codecCtx || m_frame || m_packet || m_swsCtx) {
    cleanup();
  }

  // FFmpeg 디코더 초기화
  const AVCodec *codec = avcodec_find_decoder(codecId);
  if (!codec) {
    qWarning() << "[SRTP][Step5] Decoder not found for CodecID:" << codecId;
    return false;
  }

  m_parserCtx = av_parser_init(codecId);
  if (!m_parserCtx) {
    qWarning() << "[SRTP][Step5] Could not allocate codec parser for CodecID:" << codecId;
    cleanup();
    return false;
  }

  m_codecCtx = avcodec_alloc_context3(codec);
  if (!m_codecCtx) {
    qWarning() << "[SRTP][Step5] Could not allocate video codec context";
    return false;
  }

  if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
    qWarning() << "[SRTP][Step5] Could not open codec";
    cleanup();
    return false;
  }

  m_frame = av_frame_alloc();
  m_packet = av_packet_alloc();
  if (!m_frame || !m_packet) {
    qWarning() << "[SRTP][Step5] Could not allocate frame or packet";
    cleanup();
    return false;
  }

  m_initialized = true;
  qDebug() << "[SRTP][Step5] FFmpeg Video Decoder initialized. Codec:" << codec->name;
  return true;
}

void SrtpVideoThread::decodeFrame(const QByteArray &nalUnit, uint32_t timestamp) {
  QMutexLocker locker(&m_mutex);
  Q_UNUSED(timestamp);
  if (!m_initialized || nalUnit.isEmpty()) return;

  const uint8_t *data = reinterpret_cast<const uint8_t *>(nalUnit.constData());
  int dataSize = nalUnit.size();

  while (dataSize > 0) {
    uint8_t *parsedData = nullptr;
    int parsedSize = 0;
    const int consumed = av_parser_parse2(
        m_parserCtx, m_codecCtx, &parsedData, &parsedSize, data, dataSize,
        AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (consumed < 0) {
      qWarning() << "[SRTP][Step5] Error parsing video bitstream";
      return;
    }

    data += consumed;
    dataSize -= consumed;

    if (parsedSize <= 0) {
      continue;
    }

    if (!sendPacketToDecoder(parsedData, parsedSize)) {
      return;
    }
  }
}

bool SrtpVideoThread::sendPacketToDecoder(const uint8_t *data, int size) {
  m_packet->data = const_cast<uint8_t *>(data);
  m_packet->size = size;

  int ret = avcodec_send_packet(m_codecCtx, m_packet);
  if (ret < 0) {
    qWarning() << "[SRTP][Step5] Error sending a packet for decoding";
    return false;
  }

  while (ret >= 0) {
    ret = avcodec_receive_frame(m_codecCtx, m_frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    } else if (ret < 0) {
      qWarning() << "[SRTP][Step5] Error during decoding";
      return false;
    }

    // YUV420P 등 -> BGR24 (cv::Mat 호환) 변환
    int srcRange = 0;
    const AVPixelFormat srcPixFmt =
        normalizeDeprecatedPixelFormat(static_cast<AVPixelFormat>(m_frame->format),
                                       &srcRange);
    m_swsCtx = sws_getCachedContext(m_swsCtx,
                                    m_frame->width, m_frame->height, srcPixFmt,
                                    m_frame->width, m_frame->height, AV_PIX_FMT_BGR24,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_swsCtx) {
      qWarning() << "[SRTP][Step5] Could not initialize sws context";
      continue;
    }

    const int *srcCoeffs = sws_getCoefficients(SWS_CS_DEFAULT);
    const int *dstCoeffs = sws_getCoefficients(SWS_CS_DEFAULT);
    int brightness = 0;
    int contrast = 1 << 16;
    int saturation = 1 << 16;
    sws_setColorspaceDetails(m_swsCtx, srcCoeffs, srcRange, dstCoeffs, 0,
                             brightness, contrast, saturation);

    cv::Mat image(m_frame->height, m_frame->width, CV_8UC3);
    uint8_t *dest[4] = {image.data, nullptr, nullptr, nullptr};
    int dest_linesize[4] = {static_cast<int>(image.step[0]), 0, 0, 0};

    sws_scale(m_swsCtx, m_frame->data, m_frame->linesize, 0, m_frame->height, dest, dest_linesize);

    // BGR 원본 유지, UI/OCR을 위해 전달 (QSharedPointer 사용)
    auto sharedFrame = QSharedPointer<cv::Mat>::create(std::move(image));
    
    // 타임스탬프는 시스템 시간 또는 RTP 타임스탬프 변환값을 사용할 수 있습니다.
    // 기존 시스템과 호환성을 위해 시스템 시간 사용
    qint64 currentMs = QDateTime::currentMSecsSinceEpoch();
    emit frameCaptured(sharedFrame, currentMs);
  }
  return true;
}

void SrtpVideoThread::cleanup() {
  m_initialized = false;
  
  if (m_swsCtx) {
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;
  }
  if (m_frame) {
    av_frame_free(&m_frame);
  }
  if (m_packet) {
    av_packet_free(&m_packet);
  }
  if (m_parserCtx) {
    av_parser_close(m_parserCtx);
    m_parserCtx = nullptr;
  }
  if (m_codecCtx) {
    avcodec_free_context(&m_codecCtx);
  }
}
