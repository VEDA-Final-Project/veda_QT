#ifndef SHAREDVIDEOFRAME_H
#define SHAREDVIDEOFRAME_H

#include <QMetaType>
#include <QSharedPointer>
#include <QtGlobal>
#include <opencv2/core.hpp>

struct SharedVideoFrame {
  QSharedPointer<cv::Mat> mat;
  qint64 timestampMs = 0;

  bool isValid() const { return mat && !mat->empty(); }
};

Q_DECLARE_METATYPE(SharedVideoFrame)

#endif // SHAREDVIDEOFRAME_H
