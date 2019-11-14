#include "ghexmanager.h"

#include "utils.h"

#include <QDir>
#include <QFileDialog>

#include <QDebug>

// Virtual Flash.
#define KB (1024)
#define MB (KB*KB)

// 5 MB flash
static unsigned char VirtualFlash[5*MB];

#define BOOT_SECTOR_BEGIN 0x9FC00000
#define APPLICATION_START 0x9D000000
#define PA_TO_VFA(x)	(x-APPLICATION_START)
#define PA_TO_KVA0(x)   (x|0x80000000)

#define DATA_RECORD 		0
#define END_OF_FILE_RECORD 	1
#define EXT_SEG_ADRS_RECORD 2
#define EXT_LIN_ADRS_RECORD 4


GHexManager::GHexManager(QObject *parent) : QObject(parent)
{
    HexFilePtr = NULL;
}

GHexManager::~GHexManager()
{
    if(HexFilePtr)
    {
        HexFilePtr->close();
    }
}

/****************************************************************************
 * Resets hex file pointer.
 *
 * \param
 * \param
 * \param
 * \return  True if resets file pointer.
 *****************************************************************************/
bool GHexManager::ResetHexFilePointer()
{
    // Reset file pointer.
    if(HexFilePtr == NULL)
    {
        return false;
    }
    else
    {
        HexFilePtr->seek(0);
        HexCurrLineNo = 0;
        return true;
    }
}

/****************************************************************************
 * Loads hex file
 *
 * \param
 * \param
 * \param
 * \return  true if hex file loads successfully
 *****************************************************************************/
bool GHexManager::LoadHexFile()
{
    QByteArray data;

    HexFilePath = QFileDialog::getOpenFileName(NULL,"",QDir::homePath(),"Hex File (*.hex)");

    if (HexFilePath.isEmpty()){
        return false;
    }

    HexFilePtr = new QFile(HexFilePath);

    if (HexFilePtr->open(QIODevice::ReadOnly | QIODevice::Text)){
        HexTotalLines = 0;

        while (!HexFilePtr->atEnd()) {
            data = HexFilePtr->readLine();
            // qInfo() << data;
            HexTotalLines++;
        }

        HexCurrLineNo = 0;
        //        qInfo() << "Hex" << HexTotalLines << "Lines";
    } else {
        return false;
    }

    return true;
}

/****************************************************************************
 * Gets next hex record from the hex file
 *
 * \param  HexRec: Pointer to HexRec.
 * \param  BuffLen: Buffer Length
 * \param
 * \return Length of the hex record in bytes.
 *****************************************************************************/
int GHexManager::GetNextHexRecord(char *HexRec, unsigned int BuffLen)
{
    int len = 0;
    QByteArray Ascii;
    QByteArray Hex;

    if (!HexFilePtr->atEnd()){
        Ascii = HexFilePtr->readLine(BuffLen);

        if(Ascii.at(0) != ':')
        {
            // Not a valid hex record.
            return 0;
        }

        len = Ascii.length();

        // Convert rest to hex.
        Hex = QByteArray::fromHex(Ascii.mid(1,len - 2));

        len = Hex.length();
        for(int i=0; i < len; i++)
            HexRec[i] = Hex.at(i);

        HexCurrLineNo++;

        // qInfo() << Ascii << HexCurrLineNo;
    }
    return len;
}

/****************************************************************************
 * Verifies flash
 *
 * \param  StartAddress: Pointer to program start address
 * \param  ProgLen: Pointer to Program length in bytes
 * \param  crc : Pointer to CRC
 * \return
 *****************************************************************************/
void GHexManager::VerifyFlash(unsigned int *StartAdress, unsigned int *ProgLen, unsigned short *crc)
{
    int HexRecLen;
    char HexRec[255];
    T_HEX_RECORD HexRecordSt;
    unsigned int VirtualFlashAdrs;
    unsigned int ProgAddress;

    // Virtual Flash Erase (Set all bytes to 0xFF)
    memset((void*)VirtualFlash, 0xFF, sizeof(VirtualFlash));


    // Start decoding the hex file and write into virtual flash
    // Reset file pointer.
    HexFilePtr->seek(0);

    // Reset max address and min address.
    HexRecordSt.MaxAddress = 0;
    HexRecordSt.MinAddress = 0xFFFFFFFF;

    while((HexRecLen = GetNextHexRecord(HexRec, 255)) != 0)
    {
        HexRecordSt.RecDataLen = HexRec[0];
        HexRecordSt.RecType = HexRec[3];
        HexRecordSt.Data = (unsigned char*)&HexRec[4];

        switch(HexRecordSt.RecType)
        {

        case DATA_RECORD:  //Record Type 00, data record.
            HexRecordSt.Address = (((HexRec[1] << 8) & 0x0000FF00) | (HexRec[2] & 0x000000FF)) & (0x0000FFFF);
            HexRecordSt.Address = HexRecordSt.Address + HexRecordSt.ExtLinAddress + HexRecordSt.ExtSegAddress;

            ProgAddress = PA_TO_KVA0(HexRecordSt.Address);

            if(ProgAddress < BOOT_SECTOR_BEGIN) // Make sure we are not writing boot sector.
            {
                if(HexRecordSt.MaxAddress < (ProgAddress + HexRecordSt.RecDataLen))
                {
                    HexRecordSt.MaxAddress = ProgAddress + HexRecordSt.RecDataLen;
                }

                if(HexRecordSt.MinAddress > ProgAddress)
                {
                    HexRecordSt.MinAddress = ProgAddress;
                }

                VirtualFlashAdrs = PA_TO_VFA(ProgAddress); // Program address to local virtual flash address

                memcpy((void *)&VirtualFlash[VirtualFlashAdrs], HexRecordSt.Data, HexRecordSt.RecDataLen);
            }
            break;

        case EXT_SEG_ADRS_RECORD:  // Record Type 02, defines 4 to 19 of the data address.
            HexRecordSt.ExtSegAddress = ((HexRecordSt.Data[0] << 16) & 0x00FF0000) | ((HexRecordSt.Data[1] << 8) & 0x0000FF00);
            HexRecordSt.ExtLinAddress = 0;
            break;

        case EXT_LIN_ADRS_RECORD:
            HexRecordSt.ExtLinAddress = ((HexRecordSt.Data[0] << 24) & 0xFF000000) | ((HexRecordSt.Data[1] << 16) & 0x00FF0000);
            HexRecordSt.ExtSegAddress = 0;
            break;


        case END_OF_FILE_RECORD:  //Record Type 01
        default:
            HexRecordSt.ExtSegAddress = 0;
            HexRecordSt.ExtLinAddress = 0;
            break;
        }
    }

    HexRecordSt.MinAddress -= HexRecordSt.MinAddress % 4;
    HexRecordSt.MaxAddress += HexRecordSt.MaxAddress % 4;

    *ProgLen = HexRecordSt.MaxAddress - HexRecordSt.MinAddress;
    *StartAdress = HexRecordSt.MinAddress;
    VirtualFlashAdrs = PA_TO_VFA(HexRecordSt.MinAddress);
    *crc = Utils::CalculateCrc((char*)&VirtualFlash[VirtualFlashAdrs], *ProgLen);
}
