#include "zonepanelcontroller.h"

#include "application/db/zone/zonequeryapplicationservice.h"
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <utility>

ZonePanelController::ZonePanelController(const UiRefs &uiRefs, Context context,
                                         QObject *parent)
    : QObject(parent), m_ui(uiRefs), m_context(std::move(context)) {}

void ZonePanelController::connectSignals() {
  if (m_signalsConnected) {
    return;
  }
  m_signalsConnected = true;

  if (m_ui.btnRefreshZone) {
    connect(m_ui.btnRefreshZone, &QPushButton::clicked, this,
            &ZonePanelController::refreshZoneTable);
  }
}

void ZonePanelController::refreshZoneTable() {
  if (!m_ui.zoneTable || !m_context.service) {
    return;
  }

  const QSignalBlocker blocker(m_ui.zoneTable);
  m_ui.zoneTable->setRowCount(0);

  const QVector<ZoneRow> allRecords = m_context.service->getAllZones();
  for (const ZoneRow &record : allRecords) {
    const int row = m_ui.zoneTable->rowCount();
    m_ui.zoneTable->insertRow(row);

    m_ui.zoneTable->setItem(row, 0, new QTableWidgetItem(record.cameraKey));
    m_ui.zoneTable->setItem(row, 1, new QTableWidgetItem(record.zoneName));
    m_ui.zoneTable->setItem(row, 2,
                            new QTableWidgetItem(record.occupancyLabel));
    m_ui.zoneTable->setItem(row, 3,
                            new QTableWidgetItem(record.createdAtDisplay));
  }

  appendLog(QString("주차구역 현황 갱신 완료 (%1건)").arg(allRecords.size()));
}

void ZonePanelController::appendLog(const QString &message) const {
  if (m_context.logMessage) {
    m_context.logMessage(message);
  }
}
