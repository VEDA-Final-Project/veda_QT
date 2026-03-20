#include "presentation/controllers/channelruntimecontroller.h"

#include "presentation/widgets/videowidget.h"
#include "ui/windows/camerachannelruntime.h"
#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <cstddef>
#include <utility>

ChannelRuntimeController::ChannelRuntimeController(const UiRefs &uiRefs,
                                                   Context context,
                                                   QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {
  CameraChannelRuntime::SharedUiRefs channelUiRefs;
  channelUiRefs.reidTable = nullptr;
  channelUiRefs.staleTimeoutInput = m_ui.staleTimeoutInput;
  channelUiRefs.pruneTimeoutInput = m_ui.pruneTimeoutInput;
  channelUiRefs.chkShowStaleObjects = m_ui.chkShowStaleObjects;
  channelUiRefs.avgFpsLabel = m_ui.avgFpsLabel;

  m_channels[0] = new CameraChannelRuntime(CameraChannelRuntime::Slot::Ch1,
                                           m_ui.videoWidgets[0], channelUiRefs,
                                           this);
  m_channels[1] = new CameraChannelRuntime(CameraChannelRuntime::Slot::Ch2,
                                           m_ui.videoWidgets[1], channelUiRefs,
                                           this);
  m_channels[2] = new CameraChannelRuntime(CameraChannelRuntime::Slot::Ch3,
                                           m_ui.videoWidgets[2], channelUiRefs,
                                           this);
  m_channels[3] = new CameraChannelRuntime(CameraChannelRuntime::Slot::Ch4,
                                           m_ui.videoWidgets[3], channelUiRefs,
                                           this);

  for (std::size_t i = 0; i < std::size(m_channels); ++i) {
    if (!m_channels[i]) {
      continue;
    }
    connect(m_channels[i], &CameraChannelRuntime::zoneStateChanged, this,
            &ChannelRuntimeController::zoneStateChanged);
  }
  if (m_channels[0]) {
    connect(m_channels[0], &CameraChannelRuntime::videoReady, this,
            &ChannelRuntimeController::primaryVideoReady);
  }

  m_resizeDebounceTimer = new QTimer(this);
  m_resizeDebounceTimer->setSingleShot(true);
  connect(m_resizeDebounceTimer, &QTimer::timeout, this,
          &ChannelRuntimeController::onVideoWidgetResizedSlot);

  for (VideoWidget *videoWidget : m_ui.videoWidgets) {
    if (videoWidget) {
      videoWidget->installEventFilter(this);
    }
  }
  if (m_ui.recordVideoWidget) {
    m_ui.recordVideoWidget->installEventFilter(this);
  }
  for (int i = 0; i < 4; ++i) {
    if (m_ui.channelCards[i]) {
      m_ui.channelCards[i]->installEventFilter(this);
    }
    if (m_ui.channelNameLabels[i]) {
      m_ui.channelNameLabels[i]->installEventFilter(this);
    }
    if (m_ui.thumbnailLabels[i]) {
      m_ui.thumbnailLabels[i]->installEventFilter(this);
    }
  }
}

void ChannelRuntimeController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->connectSignals();
    }
  }

  if (m_ui.chkShowFps) {
    connect(m_ui.chkShowFps, &QCheckBox::toggled, this, [this](bool checked) {
      for (CameraChannelRuntime *channel : m_channels) {
        if (channel) {
          channel->setShowFps(checked);
        }
      }
    });
    for (CameraChannelRuntime *channel : m_channels) {
      if (channel) {
        channel->setShowFps(m_ui.chkShowFps->isChecked());
      }
    }
  }
}

void ChannelRuntimeController::shutdown() {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->shutdown();
    }
  }
}

void ChannelRuntimeController::setReidPanelActiveForAll(bool active) {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->setReidPanelActive(active);
    }
  }
}

void ChannelRuntimeController::resetAllChannelZoom() {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel && channel->videoWidget()) {
      channel->videoWidget()->setZoom(1.0);
    }
  }
}

CameraChannelRuntime *ChannelRuntimeController::channelAt(int index) const {
  if (index < 0 || index >= 4) {
    return nullptr;
  }
  return m_channels[index];
}

CameraChannelRuntime *
ChannelRuntimeController::channelForCardIndex(int cardIndex) const {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel && channel->selectedCardIndex() == cardIndex) {
      return channel;
    }
  }
  return nullptr;
}

int ChannelRuntimeController::cardIndexForVideoWidget(
    const VideoWidget *videoWidget) const {
  if (!videoWidget) {
    return -1;
  }
  for (const CameraChannelRuntime *channel : m_channels) {
    if (channel && channel->videoWidget() == videoWidget) {
      return channel->selectedCardIndex();
    }
  }
  return -1;
}

bool ChannelRuntimeController::eventFilter(QObject *obj, QEvent *event) {
  if (event->type() == QEvent::Resize) {
    for (VideoWidget *videoWidget : m_ui.videoWidgets) {
      if (obj == videoWidget) {
        m_resizeDebounceTimer->start(150);
        break;
      }
    }
    if (obj == m_ui.recordVideoWidget) {
      m_resizeDebounceTimer->start(150);
    }
  }

  if (event->type() == QEvent::MouseButtonPress && m_context.handleMousePress) {
    if (m_context.handleMousePress(obj)) {
      return true;
    }
  }

  return QObject::eventFilter(obj, event);
}

void ChannelRuntimeController::onVideoWidgetResizedSlot() {
  for (CameraChannelRuntime *channel : m_channels) {
    if (channel) {
      channel->handleResizeProfileChange();
    }
  }
  if (m_context.onRecordPreviewResize) {
    m_context.onRecordPreviewResize();
  }
}
