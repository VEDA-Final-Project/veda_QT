#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui/video/videowidget.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>


class MainWindowController;
class MainWindow : public QMainWindow {
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

  // Telegram Widgets
  QLabel *m_userCountLabel = nullptr;
  QLineEdit *m_entryPlateInput = nullptr;
  QPushButton *m_btnSendEntry = nullptr;
  QLineEdit *m_exitPlateInput = nullptr;
  QSpinBox *m_feeInput = nullptr;
  QPushButton *m_btnSendExit = nullptr;
  QTableWidget *m_userTable = nullptr;

  MainWindowController *m_controller = nullptr;
};

#endif // MAINWINDOW_H
