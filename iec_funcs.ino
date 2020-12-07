#include "iec_funcs.h"
#include <FS.h>
#include <LittleFS.h>
#include "tasmota.h"

#define FILESYS LittleFS 

File myFile;

uint32_t rxLength = 0;
uint32_t rxIndex = 0;
bool eoiFlag = false;

uint8_t command[2];
bool atnState = false;
uint32_t iecState = 0;

uint8_t primary, secondary; //iec addresses
uint8_t device, channel;
uint8_t readVal;

#define MAX_LISTEN_BUFF (256)
int listenBuffCount = 0;
uint8_t listenBuff[MAX_LISTEN_BUFF];

void openFile(uint8_t *fileName, bool rw);
void writeFile(uint8_t val);
bool readFile(uint8_t *dest);
void closeFile(void);
void openFile(uint8_t *fileName, bool rw);
void printDirectory(File dir, int numTabs);
void directory(void);

enum
{
    BUS_ATN = 0,
    BUS_NOT_ATN,
    BUS_TALK,
    BUS_LISTEN,
    BUS_LISTEN_EOI
};

enum
{
    IEC_IDLE,
    IEC_PRI,
    IEC_SEC,
    IEC_CMD,
    IEC_LISTEN,
    IEC_TALK
};

enum
{
    CMD_LISTEN = 0x20,
    CMD_UNLISTEN = 0x3f,
    CMD_TALK = 0x40,
    CMD_UNTALK = 0x5F,
    CMD_OPENCHAN = 0x60,
    CMD_CLOSE = 0xE0,
    CMD_OPEN = 0xF0
};

void iecStateMachine(uint8_t op, uint8_t val);

uint8_t basicProg[18] = {
    0x01, 0x04, //load address
    0x0f, 0x04, //next line addr
    0x0a, 0x00, //line number
    0x99, 0x20, //PRINT token
    0x22, 'H', 'E', 'L', 'L', 'O', 0x22,
    0x00,      //end of line
    0x00, 0x00 //end of program
};

void onTalk(void)
{
    rxIndex = 0;
    rxLength = 18;
    eoiFlag = false;
}

uint8_t readByte(void)
{
    iecStateMachine(BUS_TALK, 0);
    //Serial.printf("<Read %02X>", readVal);
    return readVal;
    /*    if (rxLength == 1)
    {
        //flag EOI
        eoiFlag = true;
    }
    rxLength--;
    return basicProg[rxIndex++];*/
}

bool isEOI(void)
{
    bool tmpFlag = eoiFlag;
    eoiFlag = false;
    return tmpFlag;
}

//when ATN is asserted
void onATN(void)
{
    iecStateMachine(BUS_ATN, 0);
}

void onNotATN(void)
{
    iecStateMachine(BUS_NOT_ATN, 0);
}

void onSendByte(uint8_t val, uint8_t eoi)
{
    iecStateMachine(BUS_LISTEN, val);
    if (eoi)
    {
        iecStateMachine(BUS_LISTEN_EOI, 0);
    }
}

void iecStateMachine(uint8_t op, uint8_t val)
{
    //Serial.print("state:");
    //Serial.println(iecState);

    if (op == BUS_ATN)
    {
        iecState = IEC_PRI;
        primary = 0;
    }

    switch (iecState)
    {
    case IEC_IDLE:
        //do nothing - ATN gets us out of here
        break;

    case IEC_PRI: //expect primary value
        if (op == BUS_LISTEN)
        {
            primary = val;
            secondary = 0;
            iecState = IEC_SEC;
        }
        break;

    case IEC_SEC: //expect secondary address or NOT ATN
        if (op == BUS_LISTEN)
        {
            secondary = val;
            iecState = IEC_CMD;
        }
        // only a primary address
        else if (op == BUS_NOT_ATN)
        {
            //decode the command
            if (primary == CMD_UNLISTEN)
            {
                iecState = IEC_IDLE;
                //Serial.println("<BUS_UNLISTEN>");
            }
            else if (primary == CMD_UNTALK)
            {
                iecState = IEC_IDLE;
                //Serial.println("<BUS_UNTALK>");
            }
            else if ((primary & 0xe0) == CMD_LISTEN)
            {
                device = primary & 0x1f;
                iecState = IEC_LISTEN;
            }
            else if ((primary & 0xe0) == CMD_TALK)
            {
                device = primary & 0x1f;
                iecState = IEC_TALK;
            }
        }
        break;

    case IEC_CMD: //expect NOT ATN

        if (op == BUS_NOT_ATN)
        {
            //primary and secondary address

            //decode the command
            if (primary == CMD_UNLISTEN)
            {
                iecState = IEC_IDLE;
                //Serial.println("<BUS_UNLISTEN>");
            }
            else if (primary == CMD_UNTALK)
            {
                iecState = IEC_IDLE;
                //Serial.println("<BUS_UNTALK>");
            }
            else if ((primary & 0xe0) == CMD_LISTEN)
            {
                device = primary & 0x1f;
                //Serial.printf("<BUS_LISTEN:%02d>", device);
                channel = secondary & 0x0f;
                if ((secondary & 0xf0) == CMD_OPEN)
                {
                    //Serial.printf("<OPEN:%02d>", channel);
                    iecState = IEC_LISTEN;
                    listenBuffCount = 0;
                }
                else if ((secondary & 0xf0) == CMD_CLOSE)
                {
                    //Serial.printf("<CLOSE:%02d>", channel);
                    closeFile();
                }
                else if ((secondary & 0xf0) == CMD_OPENCHAN)
                {
                    //Serial.printf("<OPENCHAN:%02d>", channel);
                    iecState = IEC_LISTEN;
                    listenBuffCount = 0;
                }
            }
            else if ((primary & 0xe0) == CMD_TALK)
            {
                device = primary & 0x1f;
                //Serial.printf("<BUS_TALK:%02d>", device);
                channel = secondary & 0x0f;
                iecState = IEC_TALK;
                if ((secondary & 0xf0) == CMD_OPENCHAN)
                {
                    //Serial.printf("<CHANDATA:%02d>", channel);
                }
            }
        }
        break;

    case IEC_LISTEN:
        if (op == BUS_LISTEN)
        {
            //Serial.printf("<listen %02X>", val);
            if ((secondary & 0xf0) == CMD_OPEN)
            {
                if (listenBuffCount < MAX_LISTEN_BUFF)
                {
                    listenBuff[listenBuffCount++] = val;
                }
            }
            else if ((secondary & 0xf0) == CMD_OPENCHAN)
            {
                writeFile(val);
            }
        }
        else if (op == BUS_LISTEN_EOI)
        {
            iecState = IEC_IDLE;
            if ((secondary & 0xf0) == CMD_OPEN)
            {
                listenBuff[listenBuffCount] = 0; //terminate the string
                //Serial.printf("<Open Filename:%s>", listenBuff);
                openFile(listenBuff, !(secondary & 1));
            }
        }
        break;

    case IEC_TALK:
        if (op == BUS_TALK)
        {
            uint8_t ourBuff[10];
            //Serial.print("<talk>");
            if (readFile(ourBuff))
            {
                readVal = ourBuff[0];
                eoiFlag = false;
            }
            else
            {
                eoiFlag = true; //end of file
            }
        }
        break;
    }
}

void openFile(uint8_t *fileName, bool rw)
{

    if (device != 8)
        {
            Serial.printf("Not a disk device %d\r\n",device);
            return;
        }

    //Serial.printf("Format filesystem: %d \r\n",FILESYS.format());
    if (!FILESYS.begin())
    {
        Serial.println("initialization failed!");
    }
    //Serial.println("initialization done.");

    if (rw)
    {
        if (strcmp("$", (const char *)fileName) == 0)
        {
            //file directory requested
            //printDirectory(FILESYS.open("/","r"), 0);
            directory();
        }
        else
        {
            myFile = FILESYS.open((const char *)fileName, "r");
            if (myFile)
            {
            myFile.seek(0);
            //Serial.printf("File opened for read\r\n");
            }
        }
    }
    else
    {
        myFile = FILESYS.open((const char *)fileName, "w+");
        if (myFile)
        {
            myFile.seek(0);
            //Serial.printf("File opened for write\r\n");
        }
    }
}

void closeFile(void)
{
    if (device != 8)
        {
            Serial.printf("Not a disk device %d\r\n",device);
            return;
        }

    myFile.close();
}

bool readFile(uint8_t *dest)
{
    if (device != 8)
        {
            Serial.printf("Not a disk device %d\r\n",device);
            return false;
        }

    if ((myFile) && myFile.available())
    {
        *dest = myFile.read();
        if (myFile.available() > 0)
        {
            return true;
        }
    }
    return false;
}

char cmdBuff[100];
uint32_t cmdIndex = 0;

void writeFile(uint8_t val)
{
    if (device == 4)
    {

     //Serial.printf("<printer Write %02X>", val); 
     if (val == 0x0d)
        {
            cmdBuff[cmdIndex] = 0; //terminate the buffer
            ExecuteCommand(cmdBuff,SRC_SERIAL);
            cmdIndex = 0;
        }
     else
     {
         cmdBuff[cmdIndex++] = val;
         if (cmdIndex >= sizeof(cmdBuff))
            {
                cmdIndex = 0;   //crudely handle buffer overflow
            }
     }
     return;  
    }
    if (device != 8)
        {
            Serial.printf("Not a disk device %d\r\n",device);
            return;
        }
    if (myFile)
    {
        myFile.write(val);
        //Serial.printf("<Write %02X>", val);
    }
}

void printDirectory(File dir, int numTabs)
{
    while (true)
    {

        File entry = dir.openNextFile();
        if (!entry)
        {
            // no more files
            break;
        }
        for (uint8_t i = 0; i < numTabs; i++)
        {
            Serial.print('\t');
        }
        Serial.print(entry.name());
        if (entry.isDirectory())
        {
            Serial.println("/");
            printDirectory(entry, numTabs + 1);
        }
        else
        {
            // files have sizes, directories do not
            Serial.print("\t\t");
            Serial.println(entry.size(), DEC);
        }
        entry.close();
    }
}

void directory(void)
{
Dir dir = FILESYS.openDir("/");

    Serial.println();
    while (dir.next())
        {
        Serial.print(dir.fileName());
        if(dir.fileSize()) 
            {
            File f = dir.openFile("r");
            Serial.print("\t");
            Serial.println(f.size());
            }
        }
}
