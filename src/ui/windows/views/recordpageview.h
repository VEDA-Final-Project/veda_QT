#ifndef RECORDPAGEVIEW_H
#define RECORDPAGEVIEW_H

#include "ui/windows/mainwindowuirefs.h"
#include <QWidget>

class RecordPageView : public QWidget {
  Q_OBJECT

public:
  explicit RecordPageView(QWidget *parent = nullptr);
  const RecordUiRefs &uiRefs() const;

private:
  void setupUi();

  RecordUiRefs m_ui;
};

#endif // RECORDPAGEVIEW_H
