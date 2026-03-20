#ifndef CCTVDASHBOARDVIEW_H
#define CCTVDASHBOARDVIEW_H

#include "ui/windows/mainwindowuirefs.h"
#include <QWidget>

class CctvDashboardView : public QWidget {
  Q_OBJECT

public:
  explicit CctvDashboardView(QWidget *parent = nullptr);
  const CctvUiRefs &uiRefs() const;

private:
  void setupUi();

  CctvUiRefs m_ui;
};

#endif // CCTVDASHBOARDVIEW_H
