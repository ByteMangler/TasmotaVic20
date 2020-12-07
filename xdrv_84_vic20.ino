/*
  xdrv_84_vic20.ino - VIC20 emulation

  Copyright (C) 2020  Theo Arends and Stephan Hadinger

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

//#ifdef USE_TCP_BRIDGE

#define XDRV_84 84

#ifndef TCP_BRIDGE_CONNECTIONS
#define TCP_BRIDGE_CONNECTIONS 2 // number of maximum parallel connections
#endif

#ifndef TCP_BRIDGE_BUF_SIZE
#define TCP_BRIDGE_BUF_SIZE 255 // size of the buffer, above 132 required for efficient XMODEM
#endif

const uint16_t tcp_port = 8880;
WiFiServer *server_tcp = nullptr;
//WiFiClient   client_tcp1, client_tcp2;
WiFiClient client_tcp[TCP_BRIDGE_CONNECTIONS];
uint8_t client_next = 0;
uint8_t *tcp_buf = nullptr; // data transfer buffer

#include <TasmotaSerial.h>
TasmotaSerial *TCPSerial = nullptr;

extern void stopKey(void);

//
//  circular buffer for VIC20 terminal output
//
class circBuff
{

public:
  circBuff()
  {
    vhead = vtail = 0;
  }

  void pokeChar(char val)
  {
    vbuff[vhead++] = val;
    if (vhead >= sizeof(vbuff))
    {
      vhead = 0;
    }

    if (vhead == vtail)
    {
      vtail++;
      if (vtail >= sizeof(vbuff))
      {
        vtail = 0;
      }
    }
  }

  bool available(void)
  {
    return (vhead != vtail);
  }

  char getChar(void)
  {
    char c = 0;

    if (vhead != vtail)
    {
      c = vbuff[vtail++];
      if (vtail >= sizeof(vbuff))
      {
        vtail = 0;
      }
    }

    return c;
  }

private:
  char vbuff[100];
  uint32_t vhead = 0, vtail = 0;
};

const char kTCPCommands[] PROGMEM = "TCP"
                                    "|" // prefix
                                    "Start"
                                    "|"
                                    "Baudrate";

void (*const TCPCommand[])(void) PROGMEM = {
    &CmndTCPStart, &CmndTCPBaudrate};

uint8_t curkey = 0;

uint16_t getpc();
uint8_t getop();
void exec6502(int32_t tickcount);
void reset6502();
void irq6502();
void vic20_rtc(void);
void insert_key(uint8_t key);
bool readyForKey(void);

circBuff rxbuff, txbuff;

void serout(uint8_t val)
{
  Serial.write(val);
  txbuff.pokeChar(val); //shove into circ buff
}
uint8_t getkey()
{
  if (rxbuff.available())
  {
    curkey = rxbuff.getChar();
  }
  return (curkey);
}
void clearkey()
{
  curkey = 0;
}
void xprinthex(uint16_t val)
{
  Serial.print(val, HEX);
  Serial.println();
}

//
// Called at event loop,
//
void TCPLoop(void)
{
  uint8_t c;
  bool busy; // did we transfer some data?
  int32_t buf_len;

  // check for a new client connection
  if ((server_tcp) && (server_tcp->hasClient()))
  {
    // find an empty slot
    uint32_t i;
    for (i = 0; i < ARRAY_SIZE(client_tcp); i++)
    {
      WiFiClient &client = client_tcp[i];
      if (!client)
      {
        client = server_tcp->available();
        break;
      }
    }
    if (i >= ARRAY_SIZE(client_tcp))
    {
      i = client_next++ % ARRAY_SIZE(client_tcp);
      WiFiClient &client = client_tcp[i];
      client.stop();
      client = server_tcp->available();
    }
  }

  do
  {
    busy = false; // exit loop if no data was transferred

    // start reading the UART, this buffer can quickly overflow
    buf_len = 0;
    while ((buf_len < TCP_BRIDGE_BUF_SIZE) && (txbuff.available()))
    {
      c = txbuff.getChar();
      //  c = TCPSerial->read();
      if (c >= 0)
      {
        tcp_buf[buf_len++] = c;
        busy = true;
      }
    }

    if (buf_len > 0)
    {
      char hex_char[TCP_BRIDGE_BUF_SIZE + 1];
      //ToHex_P(tcp_buf, buf_len, hex_char, 256);
      //AddLog_P(LOG_LEVEL_DEBUG, PSTR(D_LOG_TCP "from MCU: %s"), hex_char);

      for (uint32_t i = 0; i < ARRAY_SIZE(client_tcp); i++)
      {
        WiFiClient &client = client_tcp[i];
        if (client)
        {
          client.write(tcp_buf, buf_len);
        }
      }
    }

    // handle data received from TCP
    for (uint32_t i = 0; i < ARRAY_SIZE(client_tcp); i++)
    {
      WiFiClient &client = client_tcp[i];
      buf_len = 0;
      if (client && (client.available()))
      {
        c = client.read();
        if (c >= 0)
        {
          if (c == 7) //crtl-g = STOP key
          {
            stopKey();
          }
          else
          {
            rxbuff.pokeChar(c);
            //insert_key(c); //poke into VIC20
          }
        }
      }
    }

    yield(); // avoid WDT if heavy traffic
  } while (busy);

  if (rxbuff.available() && readyForKey())
  {
    char ch = rxbuff.getChar();
    insert_key(ch); //poke the keypress across to the vic20
  }
}

/********************************************************************************************/
void TCPInit(void)
{
  tcp_buf = (uint8_t *)malloc(TCP_BRIDGE_BUF_SIZE);

  if (!tcp_buf)
  {
    AddLog_P(LOG_LEVEL_ERROR, PSTR(D_LOG_TCP "could not allocate buffer"));
    return;
  }
  AddLog_P(LOG_LEVEL_INFO, PSTR(D_LOG_TCP "Starting TCP server on port %d"), tcp_port);
  server_tcp = new WiFiServer(tcp_port);
  server_tcp->begin(); // start TCP server
  server_tcp->setNoDelay(true);
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

//
// Command `ZbConfig`
//
void CmndTCPStart(void)
{

  int32_t tcp_port = XdrvMailbox.payload;

  if (server_tcp)
  {
    AddLog_P(LOG_LEVEL_INFO, PSTR(D_LOG_TCP "Stopping TCP server"));
    server_tcp->stop();
    delete server_tcp;
    server_tcp = nullptr;

    for (uint32_t i = 0; i < ARRAY_SIZE(client_tcp); i++)
    {
      WiFiClient &client = client_tcp[i];
      client.stop();
    }
  }
  if (tcp_port > 0)
  {
    AddLog_P(LOG_LEVEL_INFO, PSTR(D_LOG_TCP "Starting TCP server on port %d"), tcp_port);
    server_tcp = new WiFiServer(tcp_port);
    server_tcp->begin(); // start TCP server
    server_tcp->setNoDelay(true);
  }

  ResponseCmndDone();
}

void CmndTCPBaudrate(void)
{
  if ((XdrvMailbox.payload >= 1200) && (XdrvMailbox.payload <= 115200))
  {
    XdrvMailbox.payload /= 1200; // Make it a valid baudrate
    Settings.tcp_baudrate = XdrvMailbox.payload;
    //TCPSerial->begin(Settings.tcp_baudrate * 1200);  // Reinitialize serial port with new baud rate
  }
  ResponseCmndNumber(Settings.tcp_baudrate * 1200);
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv84(uint8_t function)
{
  bool result = false;

  switch (function)
  {
  case FUNC_LOOP:
    //case FUNC_EVERY_50_MSECOND:
    TCPLoop();
    exec6502(8000);
    break;
  case FUNC_PRE_INIT:
    TCPInit();
    Serial.print("Init 6502\r\n");
    reset6502();
    break;

  case FUNC_SERIAL:
    insert_key(TasmotaGlobal.serial_in_byte);
    TasmotaGlobal.serial_in_byte_counter = 0;
    TasmotaGlobal.serial_in_byte = 0;
    break;

  case FUNC_COMMAND:
    result = DecodeCommand(kTCPCommands, TCPCommand);
    break;
  }
  return result;
}

//#endif // USE_TCP_BRIDGE
