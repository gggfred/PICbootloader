#ifndef GHEXMANAGER_H
#define GHEXMANAGER_H

#include <QObject>

#include <QFile>

typedef struct
{
    unsigned char RecDataLen;
    unsigned int Address;
    unsigned int MaxAddress;
    unsigned int MinAddress;
    unsigned char RecType;
    unsigned char* Data;
    unsigned char CheckSum;
    unsigned int ExtSegAddress;
    unsigned int ExtLinAddress;
}T_HEX_RECORD;

class GHexManager : public QObject
{
    Q_OBJECT
public:
    //  Constructor
    explicit GHexManager(QObject *parent = nullptr);
    //  Destructor
    ~GHexManager();

    unsigned int HexTotalLines;
    unsigned int HexCurrLineNo;
    bool ResetHexFilePointer(void);
    bool LoadHexFile(void);
    int GetNextHexRecord(char *HexRec, unsigned int BuffLen);
    void VerifyFlash(unsigned int* StartAdress, unsigned int* ProgLen, unsigned short* crc);

signals:

public slots:

private:
    QString HexFilePath;
    QFile *HexFilePtr;

};

#endif // GHEXMANAGER_H
