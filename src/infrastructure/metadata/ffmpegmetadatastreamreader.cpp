#include "infrastructure/metadata/ffmpegmetadatastreamreader.h"

#include <QThread>
#include <utility>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

FfmpegMetadataStreamReader::FfmpegMetadataStreamReader(QString url, StopCallback shouldStop)
    : m_url(std::move(url)), m_shouldStop(std::move(shouldStop)) {}

FfmpegMetadataStreamReader::~FfmpegMetadataStreamReader() {
  close();
}

bool FfmpegMetadataStreamReader::run(const ChunkCallback &onChunk,
                                     const LogCallback &log) {
  if (!open(log)) {
    return false;
  }

  AVPacket *packet = av_packet_alloc();
  if (!packet) {
    log(QStringLiteral("Could not allocate FFmpeg packet for metadata stream."));
    close();
    return false;
  }

  bool ok = true;
  while (!shouldStop()) {
    const int ret = av_read_frame(m_formatContext, packet);
    if (ret >= 0) {
      if (packet->stream_index == m_metadataStreamIndex && packet->size > 0) {
        onChunk(QByteArray(reinterpret_cast<const char *>(packet->data),
                           packet->size));
      }
      av_packet_unref(packet);
      continue;
    }

    av_packet_unref(packet);

    if (ret == AVERROR(EAGAIN)) {
      QThread::msleep(10);
      continue;
    }

    if (ret == AVERROR_EXIT && shouldStop()) {
      break;
    }

    if (ret == AVERROR_EOF) {
      log(QStringLiteral("Metadata stream ended."));
      break;
    }

    log(QStringLiteral("Metadata read failed: %1").arg(ffmpegErrorString(ret)));
    ok = false;
    break;
  }

  av_packet_free(&packet);
  close();
  return ok;
}

int FfmpegMetadataStreamReader::interruptCallback(void *opaque) {
  const auto *reader =
      static_cast<const FfmpegMetadataStreamReader *>(opaque);
  return reader && reader->shouldStop() ? 1 : 0;
}

bool FfmpegMetadataStreamReader::shouldStop() const {
  return m_shouldStop && m_shouldStop();
}

bool FfmpegMetadataStreamReader::open(const LogCallback &log) {
  close();

  avformat_network_init();

  m_formatContext = avformat_alloc_context();
  if (!m_formatContext) {
    log(QStringLiteral("Could not allocate FFmpeg format context."));
    return false;
  }

  m_formatContext->interrupt_callback.callback =
      &FfmpegMetadataStreamReader::interruptCallback;
  m_formatContext->interrupt_callback.opaque = this;

  AVDictionary *options = nullptr;
  av_dict_set(&options, "rtsp_transport", "tcp", 0);
  av_dict_set(&options, "stimeout", "2000000", 0);
  av_dict_set(&options, "rw_timeout", "2000000", 0);

  const QByteArray urlBytes = m_url.toUtf8();
  const int openResult =
      avformat_open_input(&m_formatContext, urlBytes.constData(), nullptr,
                          &options);
  av_dict_free(&options);

  if (openResult < 0) {
    if (!(openResult == AVERROR_EXIT && shouldStop())) {
      log(QStringLiteral("Could not open metadata RTSP stream: %1")
              .arg(ffmpegErrorString(openResult)));
    }
    close();
    return false;
  }

  for (unsigned int i = 0; i < m_formatContext->nb_streams; ++i) {
    const AVStream *stream = m_formatContext->streams[i];
    if (stream && stream->codecpar &&
        stream->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
      m_metadataStreamIndex = static_cast<int>(i);
      break;
    }
  }

  if (m_metadataStreamIndex < 0) {
    log(QStringLiteral("RTSP stream does not expose an FFmpeg data stream for metadata."));
    close();
    return false;
  }

  log(QStringLiteral("Metadata stream connected via FFmpeg libraries."));
  return true;
}

void FfmpegMetadataStreamReader::close() {
  m_metadataStreamIndex = -1;

  if (m_formatContext) {
    avformat_close_input(&m_formatContext);
    m_formatContext = nullptr;
  }
}

QString FfmpegMetadataStreamReader::ffmpegErrorString(int errorCode) const {
  char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(errorCode, buffer, sizeof(buffer));
  return QString::fromUtf8(buffer);
}
