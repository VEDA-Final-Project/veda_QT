#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui/video/videowidget.h"
#include <QComboBox>
#include <QCloseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTextEdit>

class MainWindowController;
class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override = default;

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void setupUi();

  VideoWidget *m_videoWidget = nullptr;
  QLineEdit *m_roiNameEdit = nullptr;
  QComboBox *m_roiPurposeCombo = nullptr;
  QComboBox *m_roiSelectorCombo = nullptr;
  QTextEdit *m_logView = nullptr;
  QPushButton *m_btnPlay = nullptr;
  QPushButton *m_btnExit = nullptr;
  QPushButton *m_btnApplyRoi = nullptr;
  QPushButton *m_btnFinishRoi = nullptr;
  QPushButton *m_btnDeleteRoi = nullptr;

  MainWindowController *m_controller = nullptr;
};

#endif // MAINWINDOW_H
