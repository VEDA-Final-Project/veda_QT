#ifndef CAMERASESSIONCONTROLLER_H
#define CAMERASESSIONCONTROLLER_H

#include <QElapsedTimer>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <array>
#include <functional>
#include <memory>
#include <opencv2/core.hpp>

#include "infrastructure/video/sharedvideoframe.h"

class CameraSource;
class QLabel;
class ParkingService;
class SharedReidRuntime;
class TelegramBotAPI;

class CameraSessionController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QLabel *thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
  };

  struct Context {
    std::function<TelegramBotAPI *()> telegramApi;
  };

  explicit CameraSessionController(const UiRefs &uiRefs, Context context,
                                   QObject *parent = nullptr);

  void start();
  void shutdown();
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  CameraSource *sourceAt(int cardIndex) const;
  ParkingService *parkingServiceForCardIndex(int cardIndex) const;
  int sourceCount() const;

signals:
  void rawFrameReady(int cardIndex, SharedVideoFrame frame);
  void statusChanged(int cardIndex, int status, const QString &detail);
  void logMessage(const QString &message);

private:
  void updateThumbnailForCard(int cardIndex, SharedVideoFrame frame);

  UiRefs m_ui;
  Context m_context;
  std::array<CameraSource *, 4> m_cameraSources{
      {nullptr, nullptr, nullptr, nullptr}};
  std::shared_ptr<SharedReidRuntime> m_sharedReidRuntime;
  bool m_reidRuntimeLogEmitted = false;
  QElapsedTimer m_renderTimerThumbs[4];
  struct ThumbnailCache {
    const cv::Mat *frameIdentity = nullptr;
    QSize labelSize;
    QPixmap pixmap;
  };
  ThumbnailCache m_thumbnailCaches[4];
};

#endif // CAMERASESSIONCONTROLLER_H
