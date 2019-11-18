#include "gbootloader.h"

#include <QDebug>

#include "utils.h"

#define SOH 01
#define EOT 04
#define DLE 16

static GBootLoader *lpParam;

static uint64_t tickCount;

GBootLoader::GBootLoader(QObject *parent)
    : QObject(parent)
{
    // Initialization of some flags and variables
    RxFrameValid = false;
    NoResponseFromDevice = false;
    TxState = FIRST_TRY;
    RxDataLen = 0;
    ResetHexFilePtr = true;

    lpParam = this;
    timer.setInterval(1);
    connect(&timer, SIGNAL(timeout()), this, SLOT(RxTxThread()));

    ComPort = new QSerialPort();
}

GBootLoader::~GBootLoader() {}

void GBootLoader::TransmitTask()
{
    static uint64_t NextRetryTimeInMs;

    switch (TxState) {
    case FIRST_TRY:
        if (RetryCount) {
            // There is something to send.
            WritePort(TxPacket, TxPacketLen);
            RetryCount--;
            // If there is no response to "first try", the command will be retried.
            TxState = RE_TRY;
            // Next retry should be attempted only after a delay.
            NextRetryTimeInMs = tickCount + TxRetryDelay;
        }
        break;

    case RE_TRY:
        if (RetryCount) {
            if (NextRetryTimeInMs < tickCount) {
                // Delay elapsed. Its time to retry.
                NextRetryTimeInMs = tickCount + TxRetryDelay;
                WritePort(TxPacket, TxPacketLen);
                // Decrement retry count.
                RetryCount--;
            }
        } else {
            // Retries Exceeded
            NoResponseFromDevice = true;
            // Reset the state
            TxState = FIRST_TRY;
        }
        break;
    }
}

/****************************************************************************
 *  Receive Task
 *
 * \param
 * \param
 * \param
 * \return
 *****************************************************************************/
void GBootLoader::ReceiveTask()
{
    unsigned short BuffLen;
    char Buff[255];

    BuffLen = ReadPort((char *) Buff, (sizeof(Buff) - 10));
    BuildRxFrame((unsigned char *) Buff, BuffLen);
    if (RxFrameValid) {
        // Valid frame is received.
        // Disable further retries.
        StopTxRetries();
        RxFrameValid = false;
        // Handle Response
        HandleResponse();
    } else {
        // Retries exceeded. There is no reponse from the device.
        if (NoResponseFromDevice) {
            // Reset flags
            NoResponseFromDevice = false;
            RxFrameValid = false;
            // Handle no response situation.
            HandleNoResponse();
        }
    }
}

/****************************************************************************
 *  Send Command
 *
 * \param		cmd:  Command
 * \param		data: Pointer to data buffer if any
 * \param 		dataLen: Data length
 * \param		retries: Number of retries allowed
 * \param		retryDelayInMs: Delay between retries in milisecond
 * \return
 *****************************************************************************/
bool GBootLoader::SendCommand(char cmd, unsigned short Retries, unsigned short DelayInMs)
{
    unsigned short crc;

    unsigned int StartAddress, Len;
    char Buff[1000];
    unsigned short BuffLen = 0;
    unsigned short HexRecLen;
    unsigned int totalRecords = 10;
    TxPacketLen = 0;

    // Store for later use.
    LastSentCommand = static_cast<T_COMMANDS>(cmd);

    switch (cmd) {
    case READ_BOOT_INFO:
        Buff[BuffLen++] = cmd;
        MaxRetry = RetryCount = Retries;
        TxRetryDelay = DelayInMs;
        break;
    case ERASE_FLASH:
        Buff[BuffLen++] = cmd;
        MaxRetry = RetryCount = Retries;
        TxRetryDelay = DelayInMs; // in ms
        break;
    case JMP_TO_APP:
        Buff[BuffLen++] = cmd;
        MaxRetry = RetryCount = 1;
        TxRetryDelay = 10; // in ms
        break;
    case PROGRAM_FLASH:
        Buff[BuffLen++] = cmd;
        if (ResetHexFilePtr) {
            if (!HexManager.ResetHexFilePointer()) {
                // Error in resetting the file pointer
                return false;
            }
        }
        HexRecLen = HexManager.GetNextHexRecord(&Buff[BuffLen], (sizeof(Buff) - 5));
        if (HexRecLen == 0) {
            //Not a valid hex file.
            return false;
        }

        BuffLen = BuffLen + HexRecLen;
        while (totalRecords) {
            HexRecLen = HexManager.GetNextHexRecord(&Buff[BuffLen], (sizeof(Buff) - 5));
            BuffLen = BuffLen + HexRecLen;
            totalRecords--;
        }
        MaxRetry = RetryCount = Retries;
        TxRetryDelay = DelayInMs; // in ms
        break;
    case READ_CRC:
        Buff[BuffLen++] = cmd;
        HexManager.VerifyFlash(static_cast<unsigned int *>(&StartAddress),
                               static_cast<unsigned int *>(&Len),
                               static_cast<unsigned short *>(&crc));
        Buff[BuffLen++] = (StartAddress);
        Buff[BuffLen++] = (StartAddress >> 8);
        Buff[BuffLen++] = (StartAddress >> 16);
        Buff[BuffLen++] = (StartAddress >> 24);
        Buff[BuffLen++] = (Len);
        Buff[BuffLen++] = (Len >> 8);
        Buff[BuffLen++] = (Len >> 16);
        Buff[BuffLen++] = (Len >> 24);
        Buff[BuffLen++] = static_cast<char>(crc);
        Buff[BuffLen++] = static_cast<char>(crc >> 8);
        MaxRetry = RetryCount = Retries;
        TxRetryDelay = DelayInMs; // in ms
        break;
    default:
        return false;
    }

    // Calculate CRC for the frame.
    crc = Utils::CalculateCrc(Buff, BuffLen);
    Buff[BuffLen++] = static_cast<char>(crc);
    Buff[BuffLen++] = static_cast<char>(crc >> 8);

    // SOH: Start of header
    TxPacket[TxPacketLen++] = SOH;

    // Form TxPacket. Insert DLE in the data field whereever SOH and EOT are present.
    for (int i = 0; i < BuffLen; i++) {
        if ((Buff[i] == EOT) || (Buff[i] == SOH) || (Buff[i] == DLE)) {
            TxPacket[TxPacketLen++] = DLE;
        }
        TxPacket[TxPacketLen++] = Buff[i];
    }

    // EOT: End of transmission
    TxPacket[TxPacketLen++] = EOT;

    return true;
}

void GBootLoader::BuildRxFrame(unsigned char *buff, unsigned short buffLen)
{
    static bool Escape = false;
    unsigned short crc;

    while ((buffLen > 0) && (RxFrameValid == false)) {
        buffLen--;

        if (RxDataLen >= (sizeof(RxData) - 2)) {
            RxDataLen = 0;
        }

        switch (*buff) {
        case SOH: //Start of header
            if (Escape) {
                // Received byte is not SOH, but data.
                RxData[RxDataLen++] = static_cast<char>(*buff);
                // Reset Escape Flag.
                Escape = false;
            } else {
                // Received byte is indeed a SOH which indicates start of new frame.
                RxDataLen = 0;
            }
            break;

        case EOT: // End of transmission
            if (Escape) {
                // Received byte is not EOT, but data.
                RxData[RxDataLen++] = static_cast<char>(*buff);
                // Reset Escape Flag.
                Escape = false;
            } else {
                // Received byte is indeed a EOT which indicates end of frame.
                // Calculate CRC to check the validity of the frame.
                if (RxDataLen > 1) {
                    crc = (RxData[RxDataLen - 2]) & 0x00ff;
                    crc = crc | ((RxData[RxDataLen - 1] << 8) & 0xFF00);
                    if ((Utils::CalculateCrc(RxData, (RxDataLen - 2)) == crc) && (RxDataLen > 2)) {
                        // CRC matches and frame received is valid.
                        RxFrameValid = true;
                    }
                }
            }
            break;

        case DLE: // Escape character received.
            if (Escape) {
                // Received byte is not ESC but data.
                RxData[RxDataLen++] = static_cast<char>(*buff);
                // Reset Escape Flag.
                Escape = false;
            } else {
                // Received byte is an escape character. Set Escape flag to escape next byte.
                Escape = true;
            }
            break;

        default: // Data field.
            RxData[RxDataLen++] = static_cast<char>(*buff);
            // Reset Escape Flag.
            Escape = false;
            break;
        }
        // Increment the pointer.
        buff++;
    }
}

/****************************************************************************
 *  Handle Response situation
 *
 * \param
 * \param
 * \param
 * \return
 *****************************************************************************/
void GBootLoader::HandleResponse()
{
    unsigned char cmd = static_cast<unsigned char>(RxData[0]);
    char majorVer = RxData[3];
    char minorVer = RxData[4];
    QString string;

    switch (cmd) {
    case READ_BOOT_INFO:
    case ERASE_FLASH:
    case READ_CRC:
        // Notify main window that command received successfully.
        emit PostMessage(cmd, &RxData[1]);
        //        ::PostMessage(m_hWnd, WM_USER_BOOTLOADER_RESP_OK, (WPARAM)cmd, (LPARAM)&RxData[1] );
        break;

    case PROGRAM_FLASH:

        // If there is a hex record, send next hex record.
        ResetHexFilePtr = false; // No need to reset hex file pointer.
        if (!SendCommand(PROGRAM_FLASH, MaxRetry, TxRetryDelay)) {
            // Notify main window that programming operation completed.
            emit PostMessage(cmd, &RxData[1]);
            //            ::PostMessage(m_hWnd, WM_USER_BOOTLOADER_RESP_OK, (WPARAM)cmd, (LPARAM)&RxData[1] );
        }
        ResetHexFilePtr = true;
        break;
    }
}

/****************************************************************************
 *  Stops transmission retries
 *
 * \param
 * \param
 * \param
 * \return
 *****************************************************************************/
void GBootLoader::StopTxRetries()
{
    // Reset state.
    TxState = FIRST_TRY;
    RetryCount = 0;
}

/****************************************************************************
 *  Gets the progress of each command. This function can be used for progress
    bar.
 *
 * \param	Lower: Pointer to current count of the progress bar.
 * \param	Upper: Pointer to max count.
 * \param
 * \return
 *****************************************************************************/
void GBootLoader::GetProgress(int *Lower, int *Upper)
{
    switch (LastSentCommand) {
    case READ_BOOT_INFO:
    case ERASE_FLASH:
    case READ_CRC:
    case JMP_TO_APP:
        // Progress with respect to retry count.
        *Lower = (MaxRetry - RetryCount);
        *Upper = MaxRetry;
        break;

    case PROGRAM_FLASH:
        // Progress with respect to line counts in hex file.
        *Lower = HexManager.HexCurrLineNo;
        *Upper = HexManager.HexTotalLines;
        break;
    }
}

/****************************************************************************
 *  Handle no response situation
 *
 * \param
 * \param
 * \param
 * \return
 *****************************************************************************/
void GBootLoader::HandleNoResponse()
{
    // Handle no response situation depending on the last sent command.
    switch (LastSentCommand) {
    case READ_BOOT_INFO:
    case ERASE_FLASH:
    case PROGRAM_FLASH:
    case JMP_TO_APP:
    case READ_CRC:
        // Notify main window that there was no reponse.
        emit PostErrorMessage(LastSentCommand, nullptr);
        //        ::PostMessage(m_hWnd, WM_USER_BOOTLOADER_NO_RESP, (WPARAM)LastSentCommand, 0 );
        break;
    }
}

/****************************************************************************
 *  Gets locally calculated CRC
 *
 * \param
 * \param
 * \param
 * \return 16 bit CRC
 *****************************************************************************/
unsigned short GBootLoader::CalculateFlashCRC()
{
    unsigned int StartAddress, Len;
    unsigned short crc;
    HexManager.VerifyFlash(static_cast<unsigned int *>(&StartAddress),
                           static_cast<unsigned int *>(&Len),
                           static_cast<unsigned short *>(&crc)
                           );
    return crc;
}

/****************************************************************************
 *  Loads hex file
 *
 * \param
 * \param
 * \param
 * \return true if hex file loads successfully
 *****************************************************************************/
bool GBootLoader::LoadHexFile()
{
    return HexManager.LoadHexFile();
}

/****************************************************************************
 *  Open communication port (USB/COM/Eth)
 *
 * \param Port Type	(USB/COM)
 * \param	com port
 * \param 	baud rate
 * \param   vid
 * \param   pid
 * \return
 *****************************************************************************/
void GBootLoader::OpenPort(T_PORTTYPE portType,
                           QString comport,
                           qint32 baud,
                           unsigned int vid,
                           unsigned int pid,
                           unsigned short skt,
                           unsigned long ip)
{
    switch (portType) {
    case USB:
        (void) vid;
        (void) pid;
        break;
    case COM:
        ComPort->setPortName(comport);
        ComPort->setBaudRate(baud);

        ComPort->open(QIODevice::ReadWrite);

        if (ComPort->isOpen())
            timer.start();
        break;
    case ETH:
        (void) skt;
        (void) ip;
        break;
    }
}

bool GBootLoader::GetPortOpenStatus(T_PORTTYPE portType)
{
    bool result = false;

    switch (portType) {
    case COM:
        result = ComPort->isOpen();
        break;
    case USB:
    case ETH:
        break;
    }

    return result;
}

/****************************************************************************
 *  Closes the communication port (USB/COM/ETH)
 *
 * \param
 * \return
 *****************************************************************************/
void GBootLoader::ClosePort(T_PORTTYPE portType)
{
    switch (portType) {
    case USB:
        break;
    case COM:
        ComPort->close();
        break;
    case ETH:
        break;
    }

    timer.stop();
}

/****************************************************************************
 *  This thread calls receive and transmit tasks
 *
 * \param lpParam  this Pointer


 * \return  0 on exit.
 *****************************************************************************/
void GBootLoader::RxTxThread()
{
    tickCount++;

    ReceiveTask();
    TransmitTask();
}

/****************************************************************************
 *  Write communication port (USB/COM/ETH)
 *
 * \param Buffer, Len
 * \return
 *****************************************************************************/
void GBootLoader::WritePort(const char *buffer, qint64 bufflen)
{
    ComPort->write(buffer, bufflen);
}

/****************************************************************************
 *  Read communication port (USB/COM/ETH)
 *
 * \param Buffer, Len
 * \return
 *****************************************************************************/
qint64 GBootLoader::ReadPort(char *buffer, qint64 bufflen)
{
    qint64 bytesRead;

    bytesRead = ComPort->read(buffer, bufflen);

    return bytesRead;
}
