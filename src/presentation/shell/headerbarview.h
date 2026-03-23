#ifndef HEADERBARVIEW_H
#define HEADERBARVIEW_H

#include "presentation/shell/mainwindowuirefs.h"
#include <QFrame>

class HeaderBarView : public QFrame {
  Q_OBJECT

public:
  explicit HeaderBarView(QWidget *parent = nullptr);
  const HeaderUiRefs &uiRefs() const;

private:
  void setupUi();

  HeaderUiRefs m_ui;
};

#endif // HEADERBARVIEW_H
