#ifndef DBPAGEVIEW_H
#define DBPAGEVIEW_H

#include "ui/windows/mainwindowuirefs.h"
#include <QWidget>

class DbPageView : public QWidget {
  Q_OBJECT

public:
  explicit DbPageView(QWidget *parent = nullptr);
  const DbUiRefs &uiRefs() const;

private:
  void setupUi();

  DbUiRefs m_ui;
};

#endif // DBPAGEVIEW_H
