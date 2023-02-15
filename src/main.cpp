#include <Arduino.h>
#include "main.h"

#define FAST_BLINK_DELAY            200
#define SLOW_BLINK_DELAY            1000

// Version
const char      versString[] = "1.0.1";

// constant pin defs used for 1) pin init and 2) copied into volatile status structure
// to maintain state of inputs pins that get written 3) pin names (nice, right?) ;-)
// NOTE: Any I/O that is connected to the DIP switches HAS to be an input because those
// switches can be strapped to ground.  Thus, if the pin was an output and a 1 was
// written, there would be a dead short on that pin (no resistors).
// NOTE: The order of the entries in this table is the order they are displayed by the
// 'pins' command. There is no other signficance to the order.
 pin_mgt_t     staticPins[] = {
  {           OCP_SCAN_LD_N, INPUT_PIN,   "OCP_SCAN_LD_N"},
  {         OCP_MAIN_PWR_EN, INPUT_PIN,   "OCP_MAIN_PWR_EN"},
  {        OCP_SCAN_DATA_IN, OUTPUT_PIN,  "OCP_SCAN_DATA_IN"},
  {           OCP_PRSNTB1_N, OUTPUT_PIN,  "OCP_PRSNTB1_N"},
  {             PCIE_PRES_N, INPUT_PIN,   "PCIE_PRES_N"},
  {              SCAN_VER_0, INPUT_PIN,   "SCAN_VER_0"},
  {       OCP_SCAN_DATA_OUT, INPUT_PIN,   "OCP_SCAN_DATA_OUT"},
  {          OCP_AUX_PWR_EN, INPUT_PIN,   "OCP_AUX_PWR_EN"},
  {            NIC_PWR_GOOD, INPUT_PIN,   "jmp_NIC_PWR_GOOD"},  // jumpered see #define for details
  {            OCP_PWRBRK_N, INPUT_PIN,   "OCP_PWRBRK_N"},
  {              OCP_BIF0_N, INPUT_PIN,   "OCP_BIF0_N"},
  {           OCP_PRSNTB3_N, OUTPUT_PIN,  "OCP_PRSNTB3_N"},
  {              FAN_ON_AUX, INPUT_PIN,   "FAN_ON_AUX"},
  {           OCP_SMB_RST_N, OUTPUT_PIN,  "OCP_SMB_RST_N"},
  {           OCP_PRSNTB0_N, OUTPUT_PIN,  "OCP_PRSNTB0_N"},
  {              OCP_BIF1_N, INPUT_PIN,   "OCP_BIF1_N"},
  {            OCP_SLOT_ID0, INPUT_PIN,   "OCP_SLOT_ID0"},
  {            OCP_SLOT_ID1, INPUT_PIN,   "OCP_SLOT_ID1"},
  {           OCP_PRSNTB2_N, OUTPUT_PIN,  "OCP_PRSNTB2_N"},
  {              SCAN_VER_1, INPUT_PIN,   "SCAN_VER_1"},
  {             PHY_RESET_N, OUTPUT_PIN,  "PHY_RESET_N"},
  {          RBT_ISOLATE_EN, OUTPUT_PIN,  "RBT_ISOLATE_EN"},
  {              OCP_BIF2_N, INPUT_PIN,   "OCP_BIF2_N"},
  {              OCP_WAKE_N, INPUT_PIN,   "OCP_WAKE_N"},
  {               TEMP_WARN, INPUT_PIN,   "TEMP_WARN"},
  {               TEMP_CRIT, INPUT_PIN,   "TEMP_CRIT"},
};

uint16_t      static_pin_count = sizeof(staticPins) / sizeof(pin_mgt_t);

// Variable data
static char         outBfr[OUTBFR_SIZE];

const char      hello[] = "Dell Xavier NIC 3.0 Test Board V";

//===================================================================
//                      setup() - Initialization
//===================================================================
void setup() 
{
  bool        LEDstate = false;
  pin_size_t  pinNo;

  // NOTE: The INA219 driver starts Wire so we don't have to
  // although it is unclear what the speed is when it does it
  //  Wire.begin();
  //  Wire.setClock(400000);

  // configure heartbeat LED pin and turn on which indicates that the
  // board is being initialized
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LEDstate);

  // configure I/O pins
  for ( int i = 0; i < static_pin_count; i++ )
  {
      pinNo = staticPins[i].pinNo;
      pinMode(pinNo, staticPins[i].pinFunc);

      // increase drive strength on output pins
      if ( staticPins[i].pinFunc == OUTPUT )
      {
          // see xavier/variants.cpp for the data in g_APinDescription[]
          // this will source 7mA, sink 10mA
          PORT->Group[g_APinDescription[pinNo].ulPort].PINCFG[g_APinDescription[pinNo].ulPin].bit.DRVSTR = 1;
      }
  }
  
  // start serial over USB and wait for a connection
  // NOTE: Baud rate isn't applicable to USB but...
  // NOTE: Many libraries won't init unless Serial
  // is running (or in this case SerialUSB). In the
  // variants.h file Serial is supposed to be
  // redirected to SerialUSB but that isn't working
  SerialUSB.begin(115200);
  while ( !SerialUSB )
  {
      // fast blink while waiting for a connection
      LEDstate = LEDstate ? 0 : 1;
      digitalWrite(PIN_LED, LEDstate);
      delay(FAST_BLINK_DELAY);
  }

  // init simulated EEPROM
  EEPROM_InitLocal();

  // init INA219's (and Wire)
  monitorsInit();

  sprintf(outBfr, "%s %s", hello, versString);
  terminalOut(outBfr);
  doPrompt();

} // setup()

//===================================================================
//                     loop() - Main Program Loop
//
// FLASHING NOTE: loop() doesn't get called until USB-serial connection
// has been established (ie, SerialUSB = 1).
//
// This loop does two things: blink the heartbeat LED and handle
// incoming characters over SerialUSB connection.  Once a full CR
// terminated input line has been received, it calls the CLI to
// parse and execute any valid command, or sends an error message.
//===================================================================
void loop() 
{
  int             byteIn;
  static char     inBfr[80];
  static int      inCharCount = 0;
  static char     lastCmd[80] = "help";
  const char      bs[4] = {0x1b, '[', '1', 'D'};  // terminal: backspace seq
  static bool     LEDstate = false;
  static uint32_t time = millis();

  // blink heartbeat LED
  if ( millis() - time >= SLOW_BLINK_DELAY )
  {
      time = millis();
      LEDstate = LEDstate ? 0 : 1;
      digitalWrite(PIN_LED, LEDstate);
  }

  // process incoming serial over USB characters
  if ( SerialUSB.available() )
  {
      byteIn = SerialUSB.read();
      if ( byteIn == 0x0a )
      {
          // line feed - echo it
          SerialUSB.write(0x0a);
          SerialUSB.flush();
      }
      else if ( byteIn == 0x0d )
      {
          // carriage return - EOL 
          // save as the last cmd (for up arrow) and call CLI with
          // the completed line less CR/LF
          terminalOut(" ");
          inBfr[inCharCount] = 0;
          inCharCount = 0;
          strcpy(lastCmd, inBfr);
          cli(inBfr);
          SerialUSB.flush();
      }
      else if ( byteIn == 0x1b )
      {
          // handle ANSI escape sequence - only UP arrow is supported
          if ( SerialUSB.available() )
          {
              byteIn = SerialUSB.read();
              if ( byteIn == '[' )
              {
                if ( SerialUSB.available() )
                {
                    byteIn = SerialUSB.read();
                    if ( byteIn == 'A' )
                    {
                        // up arrow: echo last command entered then execute in CLI
                        terminalOut(lastCmd);
                        SerialUSB.flush();
                        cli(lastCmd);
                        SerialUSB.flush();
                    }
                }
            }
        }
    }
    else if ( byteIn == 127 || byteIn == 8 )
    {
        // delete & backspace do the same thing
        if ( inCharCount )
        {
            inBfr[inCharCount] = 0;
            inCharCount--;
            SerialUSB.write(bs, 4);
            SerialUSB.write(' ');
            SerialUSB.write(bs, 4);
            SerialUSB.flush();
        }
    }
    else
    {
        // all other keys get echoed & stored in buffer
        SerialUSB.write((char) byteIn);
        SerialUSB.flush();
        inBfr[inCharCount++] = byteIn;
    }
  }

} // loop()