#ifndef MEDIARECORDERWORKER_H
#define MEDIARECORDERWORKER_H

#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <opencv2/opencv.hpp>
#include <vector>

class MediaRecorderWorker : public QObject {
  Q_OBJECT
public:
  explicit MediaRecorderWorker(QObject *parent = nullptr);

public slots:
  // 비디오 저장 요청 (메타데이터는 시그널로 반환)
  void saveVideo(const std::vector<QSharedPointer<cv::Mat>> &frames,
                 const QString &filePath, int fps = 15,
                 const QString &type = "VIDEO", const QString &description = "",
                 const QString &cameraId = "Ch1");

  // 이미지 저장 요청 (메타데이터는 시그널로 반환)
  void saveImage(QSharedPointer<cv::Mat> frame, const QString &filePath,
                 const QString &type = "IMAGE", const QString &description = "",
                 const QString &cameraId = "Ch1");

signals:
  // 저장 완료 시 메타데이터 포함해서 emit (주 스레드에서 DB 기록)
  void finished(bool success, const QString &filePath, const QString &type,
                const QString &description, const QString &cameraId);
  void error(const QString &message);
};

#endif // MEDIARECORDERWORKER_H
