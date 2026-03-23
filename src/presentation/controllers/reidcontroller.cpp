#include "presentation/controllers/reidcontroller.h"

#include "domain/parking/vehicletracker.h"
#include "infrastructure/camera/camerasource.h"
#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {
constexpr qint64 kReidRefreshIntervalMs = 300;
}

ReidController::ReidController(const UiRefs &uiRefs, Context context,
                               QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {
  m_pollTimer = new QTimer(this);
  m_pollTimer->setInterval(static_cast<int>(kReidRefreshIntervalMs));
  connect(m_pollTimer, &QTimer::timeout, this, &ReidController::onRefreshRequested);
  m_pollTimer->start();
}

void ReidController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.reidTable) {
    connect(m_ui.reidTable, &QTableWidget::cellClicked, this,
            &ReidController::onReidTableCellClicked);
  }
  if (m_ui.staleTimeoutInput) {
    connect(m_ui.staleTimeoutInput, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refresh(true); });
  }
  if (m_ui.pruneTimeoutInput) {
    connect(m_ui.pruneTimeoutInput, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { refresh(true); });
  }
  if (m_ui.chkShowStaleObjects) {
    connect(m_ui.chkShowStaleObjects, &QCheckBox::toggled, this,
            [this](bool) { refresh(true); });
  }
}

void ReidController::refresh(bool force) {
  if (!m_ui.reidTable) {
    return;
  }

  if (!force && m_refreshTimer.isValid() &&
      m_refreshTimer.elapsed() < kReidRefreshIntervalMs) {
    return;
  }
  m_refreshTimer.restart();

  QString selectedCameraKey;
  int selectedObjectId = -1;
  if (const int currentRow = m_ui.reidTable->currentRow(); currentRow >= 0) {
    if (QTableWidgetItem *objectIdItem = m_ui.reidTable->item(currentRow, 2)) {
      selectedObjectId = objectIdItem->text().toInt();
      selectedCameraKey = objectIdItem->data(Qt::UserRole).toString();
    } else if (QTableWidgetItem *idItem = m_ui.reidTable->item(currentRow, 1)) {
      selectedObjectId = idItem->data(Qt::UserRole + 1).toInt();
      selectedCameraKey = idItem->data(Qt::UserRole).toString();
    }
  }

  struct AggregatedVehicleRow {
    int cardIndex = -1;
    QString cameraKey;
    VehicleState state;
  };

  QVector<AggregatedVehicleRow> rows;
  const int staleMs =
      m_ui.staleTimeoutInput ? m_ui.staleTimeoutInput->value() : 1000;
  const bool showStaleObjects =
      !m_ui.chkShowStaleObjects || m_ui.chkShowStaleObjects->isChecked();
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const int sourceCount = m_context.sourceCount ? m_context.sourceCount() : 0;

  for (int i = 0; i < sourceCount; ++i) {
    CameraSource *source = m_context.sourceAt ? m_context.sourceAt(i) : nullptr;
    if (!source) {
      continue;
    }

    const QList<VehicleState> activeVehicles = source->activeVehicles();
    for (const VehicleState &vehicle : activeVehicles) {
      if (vehicle.objectId < 0) {
        continue;
      }
      if (!isVehicleType(vehicle.type)) {
        continue;
      }
      if (vehicle.reidId.isEmpty() || vehicle.reidId == QStringLiteral("V---")) {
        continue;
      }

      const bool isStale = (nowMs - vehicle.lastSeenMs) > staleMs;
      if (isStale && !showStaleObjects) {
        continue;
      }

      rows.append({i, source->cameraKey(), vehicle});
    }
  }

  std::sort(rows.begin(), rows.end(),
            [](const AggregatedVehicleRow &a, const AggregatedVehicleRow &b) {
              if (a.cardIndex != b.cardIndex) {
                return a.cardIndex < b.cardIndex;
              }
              return a.state.objectId < b.state.objectId;
            });

  m_ui.reidTable->setUpdatesEnabled(false);
  const QSignalBlocker blocker(m_ui.reidTable);
  m_ui.reidTable->setRowCount(static_cast<int>(rows.size()));

  int rowToRestore = -1;
  for (int i = 0; i < rows.size(); ++i) {
    const AggregatedVehicleRow &rowData = rows[i];
    const int row = i;

    const bool isStale = (nowMs - rowData.state.lastSeenMs) > staleMs;
    const QColor textColor =
        isStale ? QColor(QStringLiteral("#94A3B8"))
                : QColor(QStringLiteral("#F8FAFC"));
    const QColor rowBackground = [cardIndex = rowData.cardIndex]() {
      switch (cardIndex) {
      case 0:
        return QColor(QStringLiteral("#17324A"));
      case 1:
        return QColor(QStringLiteral("#402636"));
      case 2:
        return QColor(QStringLiteral("#183D35"));
      case 3:
        return QColor(QStringLiteral("#433A1E"));
      default:
        return QColor(QStringLiteral("#1E293B"));
      }
    }();

    auto *channelItem =
        new QTableWidgetItem(QStringLiteral("Ch%1").arg(rowData.cardIndex + 1));
    channelItem->setForeground(textColor);
    channelItem->setBackground(rowBackground);
    channelItem->setData(Qt::UserRole, rowData.cameraKey);
    m_ui.reidTable->setItem(row, 0, channelItem);

    const QString displayId =
        (rowData.state.reidId.isEmpty() ||
         rowData.state.reidId == QStringLiteral("V---"))
            ? QString("V%1").arg(rowData.state.objectId)
            : rowData.state.reidId;
    auto *idItem = new QTableWidgetItem(displayId);
    idItem->setForeground(textColor);
    idItem->setBackground(rowBackground);
    idItem->setData(Qt::UserRole, rowData.cameraKey);
    idItem->setData(Qt::UserRole + 1, rowData.state.objectId);
    idItem->setToolTip(QString("Tracker ID: %1").arg(rowData.state.objectId));
    m_ui.reidTable->setItem(row, 1, idItem);

    auto *objectIdItem =
        new QTableWidgetItem(QString::number(rowData.state.objectId));
    objectIdItem->setForeground(textColor);
    objectIdItem->setBackground(rowBackground);
    objectIdItem->setData(Qt::UserRole, rowData.cameraKey);
    m_ui.reidTable->setItem(row, 2, objectIdItem);

    auto *plateItem = new QTableWidgetItem(rowData.state.plateNumber);
    plateItem->setForeground(textColor);
    plateItem->setBackground(rowBackground);
    plateItem->setData(Qt::UserRole, rowData.cameraKey);
    m_ui.reidTable->setItem(row, 3, plateItem);

    if (rowData.state.objectId == selectedObjectId &&
        rowData.cameraKey == selectedCameraKey) {
      rowToRestore = row;
    }
  }

  if (rowToRestore >= 0) {
    m_ui.reidTable->setCurrentCell(rowToRestore, 1);
    m_ui.reidTable->selectRow(rowToRestore);
  } else {
    m_ui.reidTable->clearSelection();
    if (m_ui.btnForcePlate) {
      m_ui.btnForcePlate->setProperty("cameraKey", QString());
    }
  }

  m_ui.reidTable->setUpdatesEnabled(true);
}

void ReidController::shutdown() {
  if (m_pollTimer) {
    m_pollTimer->stop();
  }
}

void ReidController::onReidTableCellClicked(int row, int column) {
  Q_UNUSED(column);
  if (!m_ui.reidTable) {
    return;
  }

  QTableWidgetItem *idItem = m_ui.reidTable->item(row, 1);
  QTableWidgetItem *objectIdItem = m_ui.reidTable->item(row, 2);
  QTableWidgetItem *plateItem = m_ui.reidTable->item(row, 3);

  if (m_ui.forceObjectIdInput) {
    const int objectId =
        objectIdItem ? objectIdItem->text().toInt()
                     : (idItem ? idItem->data(Qt::UserRole + 1).toInt() : 0);
    m_ui.forceObjectIdInput->setValue(objectId);
  }

  if (plateItem && m_ui.forcePlateInput) {
    m_ui.forcePlateInput->setText(plateItem->text());
  }

  if (m_ui.btnForcePlate && idItem) {
    m_ui.btnForcePlate->setProperty("cameraKey",
                                    idItem->data(Qt::UserRole).toString());
  }
}

void ReidController::onRefreshRequested() { refresh(); }
