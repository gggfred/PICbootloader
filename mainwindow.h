#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "gbootloader.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    // PIC32 Bootloader functions.
    void EnableAllButtons(bool enbl);
    void ButtonStatus(unsigned int oprn);

public slots:
    unsigned int OnReceiveResponse(unsigned char cmd, char *RxDataPtrAdrs);
    unsigned int OnTransmitFailure(unsigned char cmd, char *RxDataPtrAdrs);

private slots:
    void OnTimer();

    void OnSearchDeviceTimer();

    void OnComPortError(QSerialPort::SerialPortError error);

    void on_ctrlButtonLoadHex_clicked();

    void on_ctrlButtonBootloaderVer_clicked();

    void on_ctrlButtonProgram_clicked();

    void on_ctrlButtonVerify_clicked();

    void on_ctrlButtonErase_clicked();

    void on_ctrlButtonRunApplication_clicked();

    void on_ctrlButtonEraseProgVerify_clicked();

    void on_actionBuscar_triggered();

    void on_actionAbout_triggered();

protected:
    GBootLoader mBootLoader;
    bool EraseProgVer;
    bool ConnectionEstablished = false;
    void PrintKonsole(QString string);
    void ClearKonsole(void);

    T_PORTTYPE PortSelected;

private:
    Ui::MainWindow *ui;

    QString searchPort(quint16 vid, quint16 pid);
    QString comPortName;
    void fillPortsParameters();

    QTimer timer;
    QTimer searchDevice;

    quint8 connectState;

};

#endif // MAINWINDOW_H
