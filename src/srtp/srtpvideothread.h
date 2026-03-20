#ifndef SRTPVIDEOTHREAD_H
#define SRTPVIDEOTHREAD_H

#include <QObject>
#include <QByteArray>
#include <QSharedPointer>
#include <opencv2/opencv.hpp>
#include <QMutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

/**
 * @brief SRTP 비디오 디코딩 클래스 (Step 5)
 * - RtpDepacketizer에서 재조립된 NAL Unit을 입력받아 FFmpeg avcodec으로 디코딩합니다.
 * - 디코딩된 프레임을 cv::Mat 형태로 변환하여 기존 VideoThread와 동일한 시그널을 발생시킵니다.
 */
class SrtpVideoThread : public QObject {
  Q_OBJECT
public:
  explicit SrtpVideoThread(QObject *parent = nullptr);
  ~SrtpVideoThread();

  Q_INVOKABLE bool initCodec(AVCodecID codecId = AV_CODEC_ID_H264);

public slots:
  /**
   * @brief 재조립된 NAL Unit (H.264/265 프레임) 수신
   * @param nalUnit NAL Unit 데이터
   * @param timestamp RTP 시간 스탬프
   */
  void decodeFrame(const QByteArray &nalUnit, uint32_t timestamp);

signals:
  void frameCaptured(QSharedPointer<cv::Mat> framePtr, qint64 timestampMs);
  void logMessage(const QString &msg);

private:
  void cleanup();
  bool sendPacketToDecoder(const uint8_t *data, int size);

  AVCodecContext *m_codecCtx;
  AVCodecParserContext *m_parserCtx;
  AVFrame *m_frame;
  AVPacket *m_packet;
  SwsContext *m_swsCtx;
  
  bool m_initialized;
  QMutex m_mutex;
};

#endif // SRTPVIDEOTHREAD_H
