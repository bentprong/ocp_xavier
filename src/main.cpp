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

/**
  * @name   setup
  * @brief  system initialization
  * @param  None
  * @retval None
  */
void setup() 
{
  // configure I/O pins and read all inputs
  // into pinStates[]
  // NOTE: Output pins will be 0 initially
  // then updated on any writePin()
  configureIOPins();
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  readAllPins();

  // disable main & aux power to NIC 3.0 card
  writePin(OCP_MAIN_PWR_EN, 0);
  writePin(OCP_AUX_PWR_EN, 0);

  // deassert PHY reset
  writePin(PHY_RESET_N, 1);

  // init timers for scan chain clock
  timers_Init();

  // init INA219's (and Wire)
  monitorsInit();

  // NOTE: Baud rate isn't applicable to USB...
  SerialUSB.begin(115200);

} // setup()


/**
  * @name   loop
  * @brief  main program loop
  * @param  None
  * @retval None
  * @note   blink heartbeat LED & handle incoming characters over SerialUSB connection
  */
void loop() 
{
  int             byteIn;
  static char     inBfr[MAX_LINE_SZ];
  static int      inCharCount = 0;
  static char     lastCmd[80] = "help";
  const char      bs[4] = {0x1b, '[', '1', 'D'};  // terminal: backspace seq
  static bool     LEDstate = false;
  static uint32_t time = millis();
  static bool     isFirstTime = true;

  if ( isFirstTime )
  {
    if ( SerialUSB )
    {
        doHello();
        EEPROM_InitLocal();
        terminalOut((char *) "Press ENTER if prompt is not shown");
        doPrompt();
        isFirstTime = false;
        time = millis();
    }
    else
    {
        delay(1000);
        return;
    }
  }
  else
  {
        // blink heartbeat LED
        if ( millis() - time >= SLOW_BLINK_DELAY )
        {
            time = millis();
            LEDstate = LEDstate ? 0 : 1;
            digitalWrite(PIN_LED, LEDstate);
        }
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
        inBfr[inCharCount] = byteIn;
        if ( inCharCount < (MAX_LINE_SZ-1) )
        {
            inCharCount++;
        }
        else
        {
            terminalOut((char *) "Serial input buffer overflow!");
            inCharCount = 0;
        }
    }
  }

} // loop()

/**
  * @name   
  * @brief  
  * @param  None
  * @retval None
  */