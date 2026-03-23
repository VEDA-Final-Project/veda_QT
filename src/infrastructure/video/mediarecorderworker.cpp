#include "infrastructure/video/mediarecorderworker.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace {

cv::Mat normalizeFrameForWriter(const cv::Mat &input,
                                const cv::Size &targetSize) {
  if (input.empty() || targetSize.width <= 0 || targetSize.height <= 0) {
    return {};
  }

  cv::Mat converted;

  if (input.depth() != CV_8U) {
    double scale = 1.0;
    if (input.depth() == CV_16U) {
      scale = 1.0 / 256.0;
    } else if (input.depth() == CV_32F || input.depth() == CV_64F) {
      scale = 255.0;
    }
    input.convertTo(converted, CV_MAKETYPE(CV_8U, input.channels()), scale);
  } else {
    converted = input;
  }

  cv::Mat bgr;
  switch (converted.channels()) {
  case 1:
    cv::cvtColor(converted, bgr, cv::COLOR_GRAY2BGR);
    break;
  case 3:
    bgr = converted;
    break;
  case 4:
    cv::cvtColor(converted, bgr, cv::COLOR_BGRA2BGR);
    break;
  default:
    return {};
  }

  if (bgr.size() != targetSize) {
    cv::Mat resized;
    cv::resize(bgr, resized, targetSize, 0.0, 0.0, cv::INTER_LINEAR);
    return resized;
  }

  return bgr;
}

std::vector<int> fourccCandidatesForPath(const QString &filePath) {
  const QString suffix = QFileInfo(filePath).suffix().toLower();

  if (suffix == QStringLiteral("mp4")) {
    return {
        cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
        cv::VideoWriter::fourcc('a', 'v', 'c', '1'),
        cv::VideoWriter::fourcc('H', '2', '6', '4'),
    };
  }

  if (suffix == QStringLiteral("avi")) {
    return {
        cv::VideoWriter::fourcc('X', 'V', 'I', 'D'),
        cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
    };
  }

  return {
      cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
      cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
  };
}

QString fourccToString(int fourcc) {
  const char c1 = static_cast<char>(fourcc & 0xFF);
  const char c2 = static_cast<char>((fourcc >> 8) & 0xFF);
  const char c3 = static_cast<char>((fourcc >> 16) & 0xFF);
  const char c4 = static_cast<char>((fourcc >> 24) & 0xFF);
  return QStringLiteral("%1%2%3%4").arg(c1).arg(c2).arg(c3).arg(c4);
}

} // namespace

MediaRecorderWorker::MediaRecorderWorker(QObject *parent) : QObject(parent) {}

void MediaRecorderWorker::saveVideo(
    const std::vector<QSharedPointer<cv::Mat>> &frames, const QString &filePath,
    int fps, const QString &type, const QString &description,
    const QString &cameraId) {
  if (frames.empty()) {
    emit error("No frames to save");
    emit finished(false, filePath, type, description, cameraId);
    return;
  }

  // 디렉토리 존재 확인 및 생성
  QFileInfo fileInfo(filePath);
  QDir().mkpath(fileInfo.absolutePath());

  cv::Mat firstFrame = *frames[0];
  if (firstFrame.empty()) {
    emit error("Empty first frame");
    emit finished(false, filePath, type, description, cameraId);
    return;
  }

  const cv::Size frameSize(firstFrame.cols, firstFrame.rows);
  const std::vector<int> fourccCandidates = fourccCandidatesForPath(filePath);

  cv::VideoWriter writer;
  int selectedFourcc = 0;
  for (int fourcc : fourccCandidates) {
    if (writer.open(filePath.toStdString(), fourcc, fps, frameSize, true)) {
      selectedFourcc = fourcc;
      break;
    }
  }

  if (!writer.isOpened()) {
    emit error(QString("Failed to open VideoWriter for %1")
                   .arg(QFileInfo(filePath).suffix().toLower()));
    emit finished(false, filePath, type, description, cameraId);
    return;
  }

  int writtenFrames = 0;
  int skippedFrames = 0;
  for (const auto &framePtr : frames) {
    if (!framePtr || framePtr->empty()) {
      ++skippedFrames;
      continue;
    }

    const cv::Mat normalized = normalizeFrameForWriter(*framePtr, frameSize);
    if (normalized.empty()) {
      ++skippedFrames;
      continue;
    }

    try {
      writer.write(normalized);
      ++writtenFrames;
    } catch (const cv::Exception &e) {
      writer.release();
      emit error(QString("Failed to write frame: %1").arg(e.what()));
      emit finished(false, filePath, type, description, cameraId);
      return;
    }
  }

  writer.release();

  if (writtenFrames == 0) {
    emit error("Failed to write any video frames");
    emit finished(false, filePath, type, description, cameraId);
    return;
  }

  if (skippedFrames > 0) {
    qWarning() << "[Recorder] Skipped incompatible frames:" << skippedFrames
               << "codec:" << fourccToString(selectedFourcc)
               << "targetSize:" << frameSize.width << "x" << frameSize.height;
  }

  // DB 저장은 주 스레드에서: finished 수신 후 처리
  emit finished(true, filePath, type, description, cameraId);
  qDebug() << "[Recorder] Video saved to:" << filePath << "codec:"
           << fourccToString(selectedFourcc) << "writtenFrames:"
           << writtenFrames << "skippedFrames:" << skippedFrames;
}

void MediaRecorderWorker::saveImage(QSharedPointer<cv::Mat> frame,
                                    const QString &filePath,
                                    const QString &type,
                                    const QString &description,
                                    const QString &cameraId) {
  if (!frame || frame->empty()) {
    emit error("No image frame to save");
    emit finished(false, filePath, type, description, cameraId);
    return;
  }

  // 디렉토리 존재 확인 및 생성
  QFileInfo fileInfo(filePath);
  QDir().mkpath(fileInfo.absolutePath());

  if (cv::imwrite(filePath.toStdString(), *frame)) {
    // DB 저장은 주 스레드에서: finished 수신 후 처리
    emit finished(true, filePath, type, description, cameraId);
    qDebug() << "[Recorder] Image saved to:" << filePath;
  } else {
    emit error("Failed to save image via imwrite");
    emit finished(false, filePath, type, description, cameraId);
  }
}
