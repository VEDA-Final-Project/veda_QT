#include "hardwarecontroller.h"

#include "config/config.h"
#include "presentation/widgets/controllerdialog.h"
#include "presentation/widgets/videowidget.h"
#include "infrastructure/rpi/rpicontrolclient.h"
#include <QTableWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QtGlobal>
#include <utility>

HardwareController::HardwareController(const UiRefs &uiRefs, Context context,
                                       QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)),
      m_rpiControlClient(new RpiControlClient(this)) {
  m_rpiControlClient->setServer(
      Config::instance().rpiControlHost(),
      static_cast<quint16>(Config::instance().rpiControlPort()));
}

void HardwareController::connectSignals() {
  if (m_signalsConnected || !m_rpiControlClient) {
    return;
  }
  m_signalsConnected = true;

  connect(m_rpiControlClient, &RpiControlClient::logMessage, this,
          &HardwareController::appendLog);
  connect(m_rpiControlClient, &RpiControlClient::joystickMoved, this,
          [this](const QString &dir, bool active) {
            onHardwareJoystickMoved(dir, active ? 1 : 0);
          });
  connect(m_rpiControlClient, &RpiControlClient::encoderRotated, this,
          &HardwareController::onHardwareEncoderRotated);
  connect(m_rpiControlClient, &RpiControlClient::encoderClicked, this,
          &HardwareController::onHardwareEncoderClicked);
  connect(m_rpiControlClient, &RpiControlClient::buttonPressed, this,
          &HardwareController::onHardwareButtonPressed);
  connect(m_rpiControlClient, &RpiControlClient::captureRequested, this,
          [this]() {
            if (m_context.captureManual) {
              m_context.captureManual();
            }
          });
  connect(m_rpiControlClient, &RpiControlClient::recordingChanged, this,
          &HardwareController::setHardwareRecordingState);
  connect(m_rpiControlClient, &RpiControlClient::dbTabChanged, this,
          &HardwareController::navigateHardwareToDbTab);
  connect(m_rpiControlClient, &RpiControlClient::channelSelectRequested, this,
          &HardwareController::onHardwareChannelSelectRequested);

  if (Config::instance().rpiControlAutoConnect()) {
    m_rpiControlClient->connectToServer();
  }

  if (m_ui.dbSubTabs) {
    connect(m_ui.dbSubTabs, &QTabWidget::currentChanged, this,
            &HardwareController::syncDbDataToRpi);
  }
}

void HardwareController::connectControllerDialog(ControllerDialog *dialog) {
  if (!dialog) {
    return;
  }

  connect(dialog, &ControllerDialog::simulatedJoystickMoved, this,
          &HardwareController::onHardwareJoystickMoved, Qt::UniqueConnection);
  connect(dialog, &ControllerDialog::simulatedEncoderRotated, this,
          &HardwareController::onHardwareEncoderRotated, Qt::UniqueConnection);
  connect(dialog, &ControllerDialog::simulatedEncoderClicked, this,
          &HardwareController::onHardwareEncoderClicked, Qt::UniqueConnection);
  connect(dialog, &ControllerDialog::simulatedButtonClicked, this,
          &HardwareController::onHardwareButtonPressed, Qt::UniqueConnection);
}

void HardwareController::shutdown() {
  if (m_rpiControlClient) {
    m_rpiControlClient->disconnectFromServer();
  }
  if (m_joystickTimer) {
    m_joystickTimer->stop();
  }
}

void HardwareController::processJoystickMovement() {
  if (m_context.selectedChannelCount && m_context.selectedChannelCount() > 1) {
    if (m_joystickTimer) {
      m_joystickTimer->stop();
    }
    return;
  }

  VideoWidget *videoWidget =
      m_context.primarySelectedVideoWidget
          ? m_context.primarySelectedVideoWidget()
          : nullptr;
  if (!videoWidget) {
    if (m_joystickTimer) {
      m_joystickTimer->stop();
    }
    return;
  }

  if (qFuzzyIsNull(m_joystickTargetX) && qFuzzyIsNull(m_joystickTargetY)) {
    if (m_joystickTimer) {
      m_joystickTimer->stop();
    }
    return;
  }

  videoWidget->panZoom(m_joystickTargetX, m_joystickTargetY);
}

void HardwareController::setHardwareRecordingState(bool recording) {
  if (m_context.isManualRecording &&
      m_context.isManualRecording() == recording) {
    return;
  }

  if (m_ui.btnRecordManual) {
    m_ui.btnRecordManual->setChecked(recording);
    return;
  }

  if (m_context.setManualRecording) {
    m_context.setManualRecording(recording);
  }
}

void HardwareController::navigateHardwareToDbTab(int tabIndex) {
  if (!m_ui.stackedWidget) {
    return;
  }

  m_ui.stackedWidget->setCurrentIndex(m_context.dbPageIndex);
  if (!m_ui.dbSubTabs || m_ui.dbSubTabs->count() <= 0) {
    appendLog("[RPi] DB 탭이 없어 이동할 수 없습니다.");
    return;
  }

  const int boundedIndex = qBound(0, tabIndex, m_ui.dbSubTabs->count() - 1);
  m_ui.dbSubTabs->setCurrentIndex(boundedIndex);
  syncDbDataToRpi(boundedIndex);
}

void HardwareController::onHardwareChannelSelectRequested() {
  appendLog("[RPi] $CH,SEL 은 미지원입니다. BTN 288~291을 사용하세요.");
}

void HardwareController::onHardwareButtonPressed(int btnCode) {
  if (btnCode >= 288 && btnCode <= 291) {
    if (m_context.resetAllChannelZoom) {
      m_context.resetAllChannelZoom();
    }
    if (m_context.selectSingleChannel) {
      m_context.selectSingleChannel(btnCode - 288);
    }
    return;
  }

  if (btnCode == 292) {
    if (m_ui.stackedWidget) {
      m_ui.stackedWidget->setCurrentIndex(m_context.dbPageIndex);
    }
    if (m_ui.dbSubTabs) {
      m_ui.dbSubTabs->setCurrentIndex(0);
      syncDbDataToRpi(0);
    }
    return;
  }

  if (btnCode == 293) {
    if (m_ui.stackedWidget &&
        m_ui.stackedWidget->currentIndex() == m_context.dbPageIndex &&
        m_ui.dbSubTabs && m_ui.dbSubTabs->count() > 0) {
      const int nextIndex =
          (m_ui.dbSubTabs->currentIndex() + 1) % m_ui.dbSubTabs->count();
      m_ui.dbSubTabs->setCurrentIndex(nextIndex);
    }
    return;
  }

  if (btnCode == 294) {
    if (m_context.captureManual) {
      m_context.captureManual();
    }
    return;
  }

  if (btnCode == 295) {
    if (m_ui.btnRecordManual) {
      m_ui.btnRecordManual->toggle();
    } else if (m_context.setManualRecording && m_context.isManualRecording) {
      m_context.setManualRecording(!m_context.isManualRecording());
    }
    return;
  }

  if (btnCode == 999) {
    if (m_context.selectedChannelCount && m_context.selectedChannelCount() > 1) {
      return;
    }
    VideoWidget *videoWidget =
        m_context.primarySelectedVideoWidget
            ? m_context.primarySelectedVideoWidget()
            : nullptr;
    if (videoWidget) {
      videoWidget->setZoom(1.0);
    }
    return;
  }

  appendLog(QString("[RPi] 지원하지 않는 버튼 코드: %1").arg(btnCode));
}

void HardwareController::onHardwareJoystickMoved(const QString &dir, int state) {
  if (m_context.selectedChannelCount && m_context.selectedChannelCount() > 1) {
    appendLog("[Controller] JOY Ignored (Multiple channels visible)");
    return;
  }
  if (!m_joystickTimer) {
    m_joystickTimer = new QTimer(this);
    connect(m_joystickTimer, &QTimer::timeout, this,
            &HardwareController::processJoystickMovement);
  }

  if (state == 1) {
    if (dir == "U") {
      m_joystickTargetY = -1.0;
    } else if (dir == "D") {
      m_joystickTargetY = 1.0;
    } else if (dir == "L") {
      m_joystickTargetX = -1.0;
    } else if (dir == "R") {
      m_joystickTargetX = 1.0;
    }
    if (!m_joystickTimer->isActive()) {
      m_joystickTimer->start(16);
    }
    return;
  }

  if (dir == "U" && m_joystickTargetY < 0) {
    m_joystickTargetY = 0.0;
  } else if (dir == "D" && m_joystickTargetY > 0) {
    m_joystickTargetY = 0.0;
  } else if (dir == "L" && m_joystickTargetX < 0) {
    m_joystickTargetX = 0.0;
  } else if (dir == "R" && m_joystickTargetX > 0) {
    m_joystickTargetX = 0.0;
  }
}

void HardwareController::onHardwareEncoderRotated(int delta) {
  if (m_context.selectedChannelCount && m_context.selectedChannelCount() > 1) {
    return;
  }

  VideoWidget *videoWidget =
      m_context.primarySelectedVideoWidget
          ? m_context.primarySelectedVideoWidget()
          : nullptr;
  if (videoWidget) {
    const double nextZoom = videoWidget->zoom() + ((delta > 0) ? 0.1 : -0.1);
    videoWidget->setZoom(nextZoom);
  }
}

void HardwareController::onHardwareEncoderClicked() {
  onHardwareButtonPressed(999);
}

void HardwareController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}

void HardwareController::syncDbDataToRpi(int tableIdx) {
  if (!m_rpiControlClient || !m_rpiControlClient->isConnected()) return;

  QTableWidget *targetTable = nullptr;
  if (tableIdx == 0) targetTable = m_ui.parkingLogTable;
  else if (tableIdx == 1) targetTable = m_ui.userDbTable;
  else if (tableIdx == 2) targetTable = m_ui.reidTable;
  else if (tableIdx == 3) targetTable = m_ui.zoneTable;

  if (!targetTable) return;

  QJsonArray dataArr;
  for (int r = 0; r < targetTable->rowCount(); ++r) {
    QJsonObject rowObj;
    for (int c = 0; c < targetTable->columnCount(); ++c) {
      QTableWidgetItem *item = targetTable->item(r, c);
      QString text = item ? item->text() : "";
      rowObj[QString("col%1").arg(c + 1)] = text;
    }
    dataArr.append(rowObj);
  }

  QJsonObject finalObj;
  finalObj["table_idx"] = tableIdx;
  finalObj["data"] = dataArr;

  QJsonDocument doc(finalObj);
  m_rpiControlClient->sendDbData(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}
