//===================================================================
// main.cpp
// Contains setup() initialization and main program loop()
//===================================================================
#include <Arduino.h>
#include "main.hpp"
#include "commands.hpp"
#include "eeprom.hpp"
#include "cli.hpp"

// timers
void timers_Init(void);

// heartbeat LED blink delays in ms (approx)
#define FAST_BLINK_DELAY            200
#define SLOW_BLINK_DELAY            1000

//===================================================================
//                      setup() - Initialization
//===================================================================
void setup() 
{
  bool        LEDstate = false;

  // NOTE: The INA219 driver starts Wire so we don't have to here
  // However, it is unclear what the speed is
  //  Wire.begin();
  //  Wire.setClock(400000);

  // configure heartbeat LED pin and turn on which indicates that the
  // board is being initialized (not much initialization to do!)
  // NOTE: LED is active low.
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LEDstate);

  // configure I/O pins and read all inputs
  // NOTE: Output pins will be 0 initially
  configureIOPins();
  readAllPins();

  // disable main & aux power to NIC 3.0 card
  writePin(OCP_MAIN_PWR_EN, 0);
  writePin(OCP_AUX_PWR_EN, 0);

  // deassert PHY reset
  writePin(PHY_RESET_N, 1);

  // init simulated EEPROM
  EEPROM_InitLocal();

  // init INA219's (and Wire)
  monitorsInit();

  // start serial over USB and wait for a connection
  // NOTE: Baud rate isn't applicable to USB...
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

  timers_Init();
  doHello();
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
/**
  * @name   loop
  * @brief  main program loop
  * @param  None
  * @retval None
  * @note   see comment block for more info
  */
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
          terminalOut((char *) " ");
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
        // delete & backspace do the same thing which is erase last char entered
        // and backspace once
        if ( inCharCount )
        {
            inBfr[inCharCount--] = 0;
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

/**
  * @name   
  * @brief  
  * @param  None
  * @retval None
  */