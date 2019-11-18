#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>

#include <QDebug>

#include <QSerialPort>
#include <QSerialPortInfo>

static const char blankString[] = QT_TRANSLATE_NOOP("SettingsDialog", "N/A");

#define SAVE 1
#define RESTORE 2

#define SaveButtonStatus() ButtonStatus(SAVE)
#define RestoreButtonStatus() ButtonStatus(RESTORE)

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Mask all buttons, except for "Connect Device"
    EnableAllButtons(false);

    connect(&mBootLoader,SIGNAL(PostMessage(unsigned char,char*)),this,SLOT(OnReceiveResponse(unsigned char,char*)));
    connect(&mBootLoader,SIGNAL(PostErrorMessage(unsigned char,char*)),this,SLOT(OnTransmitFailure(unsigned char,char*)));

    //  Progress Bar
    ui->progressBar->setValue(0);
    connect(&timer,SIGNAL(timeout()),this,SLOT(OnTimer()));
    timer.setInterval(100);

    //  ComPort
    comPortName.clear();
    connect(mBootLoader.ComPort,SIGNAL(error(QSerialPort::SerialPortError)),this,SLOT(OnComPortError(QSerialPort::SerialPortError)));
    connect(&searchDevice,SIGNAL(timeout()),this,SLOT(OnSearchDeviceTimer()));
    searchDevice.setInterval(500);
    searchDevice.start();

    EraseProgVer = false;
    PortSelected = COM;
    connectState = 0;
}

MainWindow::~MainWindow()
{
    delete ui;
}

/****************************************************************************
 * Enables all button, if the parameter passed is true.
 *
 *
 *****************************************************************************/
void MainWindow::EnableAllButtons(bool enbl)
{
    // Mask all buttons, except for "Connect Device"
    ui->ctrlButtonProgram->setEnabled(enbl);
    ui->ctrlButtonLoadHex->setEnabled(enbl);
    ui->ctrlButtonErase->setEnabled(enbl);
    ui->ctrlButtonVerify->setEnabled(enbl);
    ui->ctrlButtonRunApplication->setEnabled(enbl);
    //	ui->ctrlButtonConnectDevice->setEnabled(enbl);
    ui->ctrlButtonEraseProgVerify->setEnabled(enbl);
    ui->ctrlButtonBootloaderVer->setEnabled(enbl);
}

void MainWindow::ButtonStatus(unsigned int oprn)
{
    static unsigned int status;


    if(oprn == SAVE)
    {
        (ui->ctrlButtonProgram->isEnabled())? status |= 0x01: status &= ~0x01;
        (ui->ctrlButtonLoadHex->isEnabled())? status |= 0x02: status &= ~0x02;
        (ui->ctrlButtonErase->isEnabled())? status |= 0x04: status &= ~0x04;
        (ui->ctrlButtonVerify->isEnabled())? status |= 0x08: status &= ~0x08;
        (ui->ctrlButtonRunApplication->isEnabled())? status |= 0x10: status &= ~0x10;
//        (ui->ctrlButtonConnectDevice->isEnabled())? status |= 0x20: status &= ~0x20;
        (ui->ctrlButtonEraseProgVerify->isEnabled())? status |= 0x40: status &= ~0x40;
        (ui->ctrlButtonBootloaderVer->isEnabled())? status |= 0x80: status &= ~0x80;
    }
    else
    {
        // Restore
        (status & 0x01)? ui->ctrlButtonProgram->setEnabled(true): ui->ctrlButtonProgram->setEnabled(false);
        (status & 0x02)? ui->ctrlButtonLoadHex->setEnabled(true): ui->ctrlButtonLoadHex->setEnabled(false);
        (status & 0x04)? ui->ctrlButtonErase->setEnabled(true): ui->ctrlButtonErase->setEnabled(false);
        (status & 0x08)? ui->ctrlButtonVerify->setEnabled(true): ui->ctrlButtonVerify->setEnabled(false);
        (status & 0x10)? ui->ctrlButtonRunApplication->setEnabled(true): ui->ctrlButtonRunApplication->setEnabled(false);
//        (status & 0x20)? ui->ctrlButtonConnectDevice->setEnabled(true): ui->ctrlButtonConnectDevice->setEnabled(false);
        (status & 0x40)? ui->ctrlButtonEraseProgVerify->setEnabled(true): ui->ctrlButtonEraseProgVerify->setEnabled(false);
        (status & 0x80)? ui->ctrlButtonBootloaderVer->setEnabled(true): ui->ctrlButtonBootloaderVer->setEnabled(false);
    }
}

unsigned int MainWindow::OnReceiveResponse(unsigned char cmd, char *RxDataPtrAdrs)
{
    char MajorVer;// = RxData[3];
    char MinorVer ;//= RxData[4];
    char *RxData;
    QString string;
    unsigned short crc;

    RxData = RxDataPtrAdrs;
    MajorVer = RxData[0];
    MinorVer = RxData[1];

    switch(cmd)
    {
    case READ_BOOT_INFO:
        if(ConnectionEstablished == false)
        {
            // New connection.
            ClearKonsole();
            ui->lblEstado->setText("Conectado");
            string = "Dispositivo Conectado";
            PrintKonsole(string);
        }
        string = QString("Bootloader Version: %1.%2").arg(QString::number(MajorVer)).arg(QString::number(MinorVer));
        PrintKonsole(string);
        // Enable only load hex, Disconnect and erase buttons for next operation.
        ui->ctrlButtonLoadHex->setEnabled(true);
        ui->ctrlButtonErase->setEnabled(true);
        // Change the connect button to disconnect.
        ConnectionEstablished = true;
//        ui->ctrlButtonConnectDevice->setText("Desconectar");
        // Disable baud rate and com port combo boxes.
        //        ctrlComboBoxBaudRate->setEnabled(false);
        //        ctrlComboBoxComPort->setEnabled(false);
        // Disable USB VID and PID boxes.
        //        ctrlEditBoxUSBVID->setEnabled(false);
        //        ctrlEditBoxUSBPID->setEnabled(false);

        // Also enable bootloader version info.
        ui->ctrlButtonBootloaderVer->setEnabled(true);

        timer.start();
        break;

    case ERASE_FLASH:
        PrintKonsole("Flash Borrada");
        if(EraseProgVer)// Operation Erase->Program->Verify
        {
            // Erase completed. Next operation is programming.
            mBootLoader.SendCommand(PROGRAM_FLASH, 3, 500); // 500ms delay
        }
        // Restore button status to allow further operations.
        RestoreButtonStatus();
        break;

    case PROGRAM_FLASH:
        PrintKonsole("Programación completada");
        // Restore button status to allow further operations.
        RestoreButtonStatus();
        ui->ctrlButtonVerify->setEnabled(true);
        ui->ctrlButtonRunApplication->setEnabled(true);

        if(EraseProgVer)// Operation Erase->Program->Verify
        {
            // Programming completed. Next operation is verification.
            mBootLoader.SendCommand(READ_CRC, 3, 5000);// 5 second delay
        }
        break;

    case READ_CRC:
        crc = ((RxData[1] << 8) & 0xFF00) | (RxData[0] & 0x00FF);

        if(crc == mBootLoader.CalculateFlashCRC())
        {
            PrintKonsole("Verificación exitosa...");
        }
        else
        {
            PrintKonsole("Verificación fallida...");
        }
        // Reset erase->program-verify operation.
        EraseProgVer = false;
        // Restore button status to allow further operations.
        RestoreButtonStatus();
        ui->ctrlButtonVerify->setEnabled(true);
        ui->ctrlButtonRunApplication->setEnabled(true);
        break;
    }

    if(!ConnectionEstablished)
    {
        // Disable all buttons, if disconnected.
        EnableAllButtons(false);
    }
    return 1;
}

/****************************************************************************
 *  This function is invoked when there is no resonse from the device, even after
    retries.
 *
 *****************************************************************************/
unsigned int MainWindow::OnTransmitFailure(unsigned char cmd, char *RxDataPtrAdrs)
{
    EraseProgVer = false;
    switch(cmd)
    {
    case READ_BOOT_INFO:
    case ERASE_FLASH:
    case PROGRAM_FLASH:
    case READ_CRC:
        PrintKonsole("Sin respuesta del dispositivo. Operacion fallida");
        connectState = 2;
        RestoreButtonStatus();
        break;
    }

    if(!ConnectionEstablished)
    {
        // Disable all buttons, if disconnected.
        EnableAllButtons(false);
    }

    return 1;
}

/****************************************************************************
 * Just do some mundane things here, like getting progress bar status,
    disabling buttons if connection cuts.
   This function is called everytime the timer elapses.
 *
 *
 *****************************************************************************/
void MainWindow::OnTimer()
{
    int Lower;
    int Upper;

    mBootLoader.GetProgress(&Lower, &Upper);
    ui->progressBar->setRange(0,Upper);
    ui->progressBar->setValue(Lower);

}

void MainWindow::OnSearchDeviceTimer()
{
    switch (connectState) {
    case 0: //  Cheque estado de la conexion, si está desconectado intenta conectar
        if (!mBootLoader.GetPortOpenStatus(PortSelected)){
            connectState = 1;
        }
        break;
    case 1: //  Intenta conectar
        ui->lblEstado->setText("Buscando dispositivo");
        comPortName = searchPort(0x0403,0x6015);

        if (!comPortName.isEmpty()){
            // Establish new connection.
            comPortName = searchPort(0x0403,0x6015);

            if(mBootLoader.GetPortOpenStatus(PortSelected))
            {
                // com port already opened. close com port
                mBootLoader.ClosePort(PortSelected);
            }
            // Open Communication port freshly.
            mBootLoader.OpenPort(COM,comPortName,QSerialPort::Baud115200,0,0,0,0);

            connectState = 0;

            if (mBootLoader.GetPortOpenStatus(PortSelected))
            {   // COM port opened.
                // Trigger Read boot info command
                mBootLoader.SendCommand(READ_BOOT_INFO,30,200);

                // Print a message to user/
                PrintKonsole("Por favor reinicie el dispositivo e invoque el bootloader");
            }
        }
        break;
    case 2: //  Desconecta
        // Already connected. Disconnect now.
        ConnectionEstablished = false;

        mBootLoader.ClosePort(COM);

        // Print console.
        ui->lblEstado->setText("Desconectado");
        PrintKonsole("Dispositivo desconectado");

//        ui->ctrlButtonConnectDevice->setText("Conectar");
        connectState = 0;
        this->searchDevice.stop();
        break;
    default:
        break;
    }
}

void MainWindow::OnComPortError(QSerialPort::SerialPortError error)
{
    switch (error) {
    case QSerialPort::ResourceError:
        connectState = 2;
        break;
    case QSerialPort::DeviceNotFoundError:
        QMessageBox::warning(this,tr("Bootloader"),tr("El puerto seleccionado no existe"),QMessageBox::Ok);
        break;
    default:
        break;
    }
    if (error != QSerialPort::NoError){
        qInfo() << "Error serial" << error;
    }
}

QString MainWindow::searchPort(quint16 vid, quint16 pid)
{
    QString description;
    QString manufacturer;
    QString portName = "";

    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
        QStringList list;
        description = info.description();
        manufacturer = info.manufacturer();

        list << info.portName()
             << (!description.isEmpty() ? description : blankString)
             << (!manufacturer.isEmpty() ? manufacturer : blankString)
             << info.systemLocation()
             << (info.vendorIdentifier() ? QString::number(info.vendorIdentifier(), 16) : blankString)
             << (info.productIdentifier() ? QString::number(info.productIdentifier(), 16) : blankString);


        if ((info.vendorIdentifier() == vid) && (info.productIdentifier() == pid)){
            portName = info.portName();
        }
    }
    return portName;
}

void MainWindow::PrintKonsole(QString string)
{
    ui->textBrowser->append(string);
}

/****************************************************************************
 * Clears konsole
 *
 *
 *****************************************************************************/
void MainWindow::ClearKonsole()
{
    ui->textBrowser->clear();
}

void MainWindow::on_ctrlButtonLoadHex_clicked()
{
    // Save button status.
    SaveButtonStatus();
    if (mBootLoader.LoadHexFile()){
        PrintKonsole("Archivo Hex cargado exitosamente");
        // Enable Program button
        ui->ctrlButtonProgram->setEnabled(true);
        ui->ctrlButtonEraseProgVerify->setEnabled(true);
    } else{
        PrintKonsole("Archivo Hex carga fallida");
    }
}

/****************************************************************************
 * This function is invoked when button Read Version is clicked
 *
 *
 *****************************************************************************/
void MainWindow::on_ctrlButtonBootloaderVer_clicked()
{
    // TODO: Add your control notification handler code here
    // Save button status.
    SaveButtonStatus();
    // Disable all buttons to avoid further operations
    EnableAllButtons(false);
    mBootLoader.SendCommand(READ_BOOT_INFO, 50, 200);
}

/****************************************************************************
 * Invoked when the button program is clicked.
 *
 *
 *****************************************************************************/
void MainWindow::on_ctrlButtonProgram_clicked()
{
    // Save button status.
    SaveButtonStatus();
    // Disable all buttons to avoid further operations
    EnableAllButtons(false);
    mBootLoader.SendCommand(PROGRAM_FLASH, 3, 5000); // 500ms delay
}

/****************************************************************************
 * This function is invoked when button verify is clicked
 *
 *
 *****************************************************************************/
void MainWindow::on_ctrlButtonVerify_clicked()
{
    // Save button status.
    SaveButtonStatus();
    // Disable all buttons to avoid further operations
    EnableAllButtons(false);
    mBootLoader.SendCommand(READ_CRC, 3, 5000);
}

/****************************************************************************
 * Invoked when button erase is clicked.
 *
 *
 *****************************************************************************/
void MainWindow::on_ctrlButtonErase_clicked()
{
    SaveButtonStatus();
    // Disable all buttons, to avoid further operation.
    EnableAllButtons(false);
    mBootLoader.SendCommand(ERASE_FLASH, 3, 5000); //5s retry delay, becuse erase takes considerable time.
}

/****************************************************************************
 * This function is invoked when button run application is clicked
 *
 *
 *****************************************************************************/
void MainWindow::on_ctrlButtonRunApplication_clicked()
{

    mBootLoader.SendCommand(JMP_TO_APP, 1, 10); // 10ms delay
    PrintKonsole("\nEnviado comando para iniciar aplicación");
}

/****************************************************************************
 * This function is invoked when button Erase-Program-Verify is clicked
 *
 *
 *****************************************************************************/
void MainWindow::on_ctrlButtonEraseProgVerify_clicked()
{
    // TODO: Add your control notification handler code here
    // Save button status.
    SaveButtonStatus();
    // Disable all buttons to avoid further operations
    EnableAllButtons(false);

    EraseProgVer = true;
    // Start with erase. Rest is automatically handled by state machine.
    mBootLoader.SendCommand(ERASE_FLASH, 3, 5000); // 5s delay
}

void MainWindow::on_actionBuscar_triggered()
{
    ui->textBrowser->clear();
    searchDevice.setInterval(500);
    searchDevice.start();
}

void MainWindow::on_actionAbout_triggered()
{
    QString text = QString("Autor: Galo Guzmán G.\n");

    QMessageBox::about(this, "Acerca de Gestor de Actualización", text);
}
