#include "video/mediarecorderworker.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

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

  cv::Size frameSize(firstFrame.cols, firstFrame.rows);
  int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');

  cv::VideoWriter writer;
  if (!writer.open(filePath.toStdString(), fourcc, fps, frameSize)) {
    fourcc = cv::VideoWriter::fourcc('X', 'V', 'I', 'D');
    if (!writer.open(filePath.toStdString(), fourcc, fps, frameSize)) {
      emit error("Failed to open VideoWriter");
      emit finished(false, filePath, type, description, cameraId);
      return;
    }
  }

  for (const auto &framePtr : frames) {
    if (framePtr && !framePtr->empty()) {
      cv::Mat bgrFrame;
      cv::cvtColor(*framePtr, bgrFrame, cv::COLOR_RGB2BGR);
      if (bgrFrame.size() != frameSize) {
        cv::resize(bgrFrame, bgrFrame, frameSize);
      }
      writer.write(bgrFrame);
    }
  }

  writer.release();
  // DB 저장은 주 스레드에서: finished 수신 후 처리
  emit finished(true, filePath, type, description, cameraId);
  qDebug() << "[Recorder] Video saved to:" << filePath;
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

  cv::Mat bgrFrame;
  cv::cvtColor(*frame, bgrFrame, cv::COLOR_RGB2BGR);
  if (cv::imwrite(filePath.toStdString(), bgrFrame)) {
    // DB 저장은 주 스레드에서: finished 수신 후 처리
    emit finished(true, filePath, type, description, cameraId);
    qDebug() << "[Recorder] Image saved to:" << filePath;
  } else {
    emit error("Failed to save image via imwrite");
    emit finished(false, filePath, type, description, cameraId);
  }
}
