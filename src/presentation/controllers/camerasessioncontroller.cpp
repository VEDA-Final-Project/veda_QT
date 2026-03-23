#include "presentation/controllers/camerasessioncontroller.h"

#include "application/parking/parkingservice.h"
#include "infrastructure/camera/camerasource.h"
#include "config/config.h"
#include "infrastructure/telegram/telegrambotapi.h"
#include <QImage>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <utility>

namespace {
constexpr int kCameraStartStaggerMs = 0;
constexpr int kCameraSlotCount = 4;
}

CameraSessionController::CameraSessionController(const UiRefs &uiRefs,
                                                 Context context,
                                                 QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void CameraSessionController::start() {
  QStringList cameraKeys = Config::instance().cameraKeys();
  if (cameraKeys.isEmpty()) {
    cameraKeys << QStringLiteral("camera");
  }

  for (int i = 0; i < static_cast<int>(m_cameraSources.size()); ++i) {
    if (i >= cameraKeys.size()) {
      continue;
    }
    if (m_cameraSources[static_cast<size_t>(i)]) {
      continue;
    }

    CameraSource *source = new CameraSource(cameraKeys[i], i, this);
    source->initialize(m_context.telegramApi ? m_context.telegramApi() : nullptr);
    connect(source, &CameraSource::rawFrameReady, this,
            [this](int cardIndex, QSharedPointer<cv::Mat> framePtr,
                   qint64 timestampMs) {
              emit rawFrameReady(cardIndex,
                                 SharedVideoFrame{framePtr, timestampMs});
            });
    connect(source, &CameraSource::thumbnailFrameReady, this,
            [this](int cardIndex, const QImage &image) {
              if (cardIndex < 0 || cardIndex >= kCameraSlotCount ||
                  image.isNull() || !m_ui.thumbnailLabels[cardIndex]) {
                return;
              }

              if (m_renderTimerThumbs[cardIndex].isValid() &&
                  m_renderTimerThumbs[cardIndex].elapsed() < 200) {
                return;
              }
              m_renderTimerThumbs[cardIndex].restart();

              const QSize targetSize = m_ui.thumbnailLabels[cardIndex]->size();
              ThumbnailCache &cache = m_thumbnailCaches[cardIndex];
              if (cache.labelSize != targetSize || cache.pixmap.isNull()) {
                cache.frameIdentity = nullptr;
                cache.labelSize = targetSize;
                cache.pixmap = QPixmap::fromImage(
                    image.scaled(targetSize, Qt::KeepAspectRatio,
                                 Qt::FastTransformation));
              }
              m_ui.thumbnailLabels[cardIndex]->setText(QString());
              QMetaObject::invokeMethod(m_ui.thumbnailLabels[cardIndex],
                                        "setPixmap", Qt::QueuedConnection,
                                        Q_ARG(QPixmap, cache.pixmap));
            });
    connect(source, &CameraSource::statusChanged, this,
            [this](int cardIndex, CameraSource::Status status,
                   const QString &detail) {
              emit statusChanged(cardIndex, static_cast<int>(status), detail);
            });
    connect(source, &CameraSource::logMessage, this,
            &CameraSessionController::logMessage);
    m_cameraSources[static_cast<size_t>(i)] = source;

    QPointer<CameraSource> deferredSource(source);
    const int startDelayMs = i * kCameraStartStaggerMs;
    QTimer::singleShot(startDelayMs, this, [this, deferredSource, i, startDelayMs]() {
      if (!deferredSource) {
        return;
      }
      emit logMessage(QString("[Camera] Ch %1 시작 예약 실행 (%2 ms)")
                          .arg(i + 1)
                          .arg(startDelayMs));
      deferredSource->start();
    });
  }
}

void CameraSessionController::shutdown() {
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->stop();
    }
  }
}

void CameraSessionController::updateObjectFilter(
    const QSet<QString> &disabledTypes) {
  for (CameraSource *source : m_cameraSources) {
    if (source) {
      source->updateObjectFilter(disabledTypes);
    }
  }
}

CameraSource *CameraSessionController::sourceAt(int cardIndex) const {
  if (cardIndex < 0 || cardIndex >= static_cast<int>(m_cameraSources.size())) {
    return nullptr;
  }
  return m_cameraSources[static_cast<size_t>(cardIndex)];
}

ParkingService *
CameraSessionController::parkingServiceForCardIndex(int cardIndex) const {
  CameraSource *source = sourceAt(cardIndex);
  return source ? source->parkingService() : nullptr;
}

int CameraSessionController::sourceCount() const { return kCameraSlotCount; }

void CameraSessionController::updateThumbnailForCard(int cardIndex,
                                                     SharedVideoFrame frame) {
  if (cardIndex < 0 || cardIndex >= kCameraSlotCount || !frame.isValid() ||
      !m_ui.thumbnailLabels[cardIndex]) {
    return;
  }

  if (m_renderTimerThumbs[cardIndex].isValid() &&
      m_renderTimerThumbs[cardIndex].elapsed() < 200) {
    return;
  }
  m_renderTimerThumbs[cardIndex].restart();

  const QSize targetSize = m_ui.thumbnailLabels[cardIndex]->size();
  ThumbnailCache &cache = m_thumbnailCaches[cardIndex];
  const cv::Mat *frameIdentity = frame.mat.data();
  if (cache.frameIdentity != frameIdentity || cache.labelSize != targetSize ||
      cache.pixmap.isNull()) {
    QImage image(frame.mat->data, frame.mat->cols, frame.mat->rows,
                 frame.mat->step, QImage::Format_BGR888);
    const QImage scaledImg =
        image.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    cache.frameIdentity = frameIdentity;
    cache.labelSize = targetSize;
    cache.pixmap = QPixmap::fromImage(scaledImg);
  }
  m_ui.thumbnailLabels[cardIndex]->setText(QString());
  QMetaObject::invokeMethod(m_ui.thumbnailLabels[cardIndex], "setPixmap",
                            Qt::QueuedConnection,
                            Q_ARG(QPixmap, cache.pixmap));
}
