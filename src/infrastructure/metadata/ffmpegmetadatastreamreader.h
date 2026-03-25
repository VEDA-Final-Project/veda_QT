#ifndef FFMPEGMETADATASTREAMREADER_H
#define FFMPEGMETADATASTREAMREADER_H

#include <QByteArray>
#include <QString>
#include <functional>

struct AVFormatContext;

class FfmpegMetadataStreamReader {
public:
  using StopCallback = std::function<bool()>;
  using ChunkCallback = std::function<void(const QByteArray &)>;
  using LogCallback = std::function<void(const QString &)>;

  FfmpegMetadataStreamReader(QString url, StopCallback shouldStop);
  ~FfmpegMetadataStreamReader();

  bool run(const ChunkCallback &onChunk, const LogCallback &log);

private:
  static int interruptCallback(void *opaque);

  bool shouldStop() const;
  bool open(const LogCallback &log);
  void close();
  QString ffmpegErrorString(int errorCode) const;

  QString m_url;
  StopCallback m_shouldStop;
  AVFormatContext *m_formatContext = nullptr;
  int m_metadataStreamIndex = -1;
};

#endif // FFMPEGMETADATASTREAMREADER_H
