#ifndef GBOOTLOADER_H
#define GBOOTLOADER_H

#include <QObject>
#include <QThread>

#include "ghexmanager.h"
#include <QSerialPort>

#include <QTimer>

// Trasnmission states
#define FIRST_TRY 0
#define RE_TRY 1

// Commands
typedef enum
{
    READ_BOOT_INFO = 1,
    ERASE_FLASH,
    PROGRAM_FLASH,
    READ_CRC,
    JMP_TO_APP

}T_COMMANDS;

typedef enum
{
    USB,
    COM,
    ETH
}T_PORTTYPE;

class GBootLoader : public QObject
{
    Q_OBJECT
public:
    // Constructor
    explicit GBootLoader(QObject *parent = nullptr);
    // Destructor
    ~GBootLoader();

    bool ExitThread;
    bool ThreadKilled;
    QSerialPort *ComPort;

    void TransmitTask(void);
    bool SendCommand(char cmd, unsigned short Retries, unsigned short DelayInMs);
    void BuildRxFrame(unsigned char*buff, unsigned short buffLen);
    void StopTxRetries(void);
    void HandleResponse(void);
    void HandleNoResponse(void);
    void GetProgress(int *Lower, int *Upper);
    unsigned short CalculateFlashCRC(void);
    bool LoadHexFile(void);
    void OpenPort(T_PORTTYPE portType, QString comport, qint32 baud, unsigned int vid, unsigned int pid, unsigned short skt, unsigned long ip);
    bool GetPortOpenStatus(T_PORTTYPE portType);
    void ClosePort(T_PORTTYPE portType);

signals:
    void PostMessage(unsigned char, char*);
    void PostErrorMessage(unsigned char, char*);

public slots:
    void RxTxThread();
    void ReceiveTask(void);

private:
    char TxPacket[1000];
    unsigned short TxPacketLen;
    char RxData[255];
    unsigned short RxDataLen;
    unsigned short RetryCount;

    bool RxFrameValid;
    T_COMMANDS LastSentCommand;
    bool NoResponseFromDevice;
    unsigned int TxState;
    unsigned short MaxRetry;
    unsigned short TxRetryDelay;
    GHexManager HexManager;
    bool ResetHexFilePtr;
    void WritePort(const char *buffer, qint64 bufflen);
    qint64 ReadPort(char *buffer, qint64 bufflen);


    QTimer timer;

};

#endif // GBOOTLOADER_H
