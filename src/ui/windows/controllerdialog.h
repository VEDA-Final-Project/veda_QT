#ifndef CONTROLLERDIALOG_H
#define CONTROLLERDIALOG_H

#include <QDialog>
#include <QString>

class QPushButton;

class ControllerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ControllerDialog(QWidget *parent = nullptr);
    ~ControllerDialog() override = default;

signals:
    // Simulated RPi Signals
    void simulatedJoystickMoved(const QString &dir, int state);
    void simulatedEncoderRotated(int delta);
    void simulatedEncoderClicked();
    void simulatedButtonClicked(int btnCode);

private slots:
    void onJoystickPressed(const QString &dir);
    void onJoystickReleased(const QString &dir);

private:
    void setupUi();

    // UI Buttons
    QPushButton *m_btnJoyUp = nullptr;
    QPushButton *m_btnJoyDown = nullptr;
    QPushButton *m_btnJoyLeft = nullptr;
    QPushButton *m_btnJoyRight = nullptr;

    QPushButton *m_btnZoomIn = nullptr;
    QPushButton *m_btnZoomOut = nullptr;
    QPushButton *m_btnZoomReset = nullptr;

    QPushButton *m_btnCh1 = nullptr;
    QPushButton *m_btnCh2 = nullptr;
    QPushButton *m_btnCh3 = nullptr;
    QPushButton *m_btnCh4 = nullptr;

    QPushButton *m_btnDbMove = nullptr;
    QPushButton *m_btnDbTab = nullptr;
    QPushButton *m_btnCapture = nullptr;
    QPushButton *m_btnRecord = nullptr;
};

#endif // CONTROLLERDIALOG_H
