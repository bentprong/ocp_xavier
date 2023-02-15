#include <Arduino.h>
#include "main.h"
#include "Wire.h"

extern uint8_t          eepromAddresses[];
extern EEPROM_data_t    EEPROMData;
static char             outBfr[OUTBFR_SIZE];
extern char             *tokens[];

//===================================================================
//                     DEBUG FUNCTIONS
//===================================================================

// --------------------------------------------
// dumpMem() - debug utility to dump memory
// --------------------------------------------
void dumpMem(unsigned char *s, int len)
{
    char        lineBfr[100];
    char        *t = lineBfr;
    char        ascii[20];
    char        *a = ascii;
    int         lc = 0;
    int         i = 0;

    while ( i < len )
    {
        sprintf(t, "%02x ", *s);
        t += 3;
        i++;

        if ( isprint(*s) )
            *a = *s;
        else
            *a = '.';

        s++;
        a++;

        // 16 bytes per line + ascii representation
        if ( ++lc == 16 )
        {
            *t = 0;
            *a = 0;
            sprintf(outBfr, "%s | %s |", lineBfr, ascii);
            terminalOut(outBfr);

            lc = 0;
            t = lineBfr;
            a = ascii;
        }
    }

    *t = 0;
    *a = 0;

    if ( lineBfr[0] != 0 )
    {
        sprintf(outBfr, "%s | %s |", lineBfr, ascii);
        terminalOut(outBfr);
    }

} // dumpMem()

// --------------------------------------------
// debug_scan() - I2C bus scanner
//
// While not associated with the board function,
// this was developed in order to locate the
// temp sensor. Left in for future use.
// --------------------------------------------
void debug_scan(void)
{
  byte        count = 0;
  int         scanCount = 0;
  uint32_t    startTime = millis();
  const char        *s;

  terminalOut ((char *) "Scanning I2C bus...");

  for (byte i = 8; i < 120; i++)
  {
    scanCount++;
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0)
    {
      if ( i == 0x40 )
        s = "U2 INA219";
      else if ( i == 0x41 )
        s = "U3 INA219";
      else
      {
        s = "Unknown device";
        for ( int j = 0; j < 4; j++ )
        {

            if ( eepromAddresses[j] == i )
            {
                s = "FRU EEPROM";
                break;
            }
        }
      }

      sprintf(outBfr, "Found device at address %d 0x%2X %s ", i, i, s);
      terminalOut(outBfr);
      count++;
      delay(10);  
    } 
  } 

  sprintf(outBfr, "Scan complete, %d addresses scanned in %lu ms", scanCount, millis() - startTime);
  terminalOut(outBfr);

  if ( count )
  {
    sprintf(outBfr, "Found %d I2C device(s)", count);
    terminalOut(outBfr);
  }
  else
  {
    terminalOut((char *) "No I2c device found");
  }
}

// --------------------------------------------
// debug_reset() - force board reset
// --------------------------------------------
void debug_reset(void)
{
    terminalOut((char *) "Board reset will disconnect USB-serial connection now.");
    terminalOut((char *) "Repeat whatever steps you took to connect to the board.");
    delay(1000);
    NVIC_SystemReset();
}

// --------------------------------------------
// debug_dump_eeprom() - Dump EEPROM contents
// --------------------------------------------
void debug_dump_eeprom(void)
{
    terminalOut((char *) "EEPROM Contents:");
    sprintf(outBfr, "Signature: %08X", (unsigned int) EEPROMData.sig);
    terminalOut(outBfr);
}

// --------------------------------------------
// debug() - Main debug program
//
// arg = number of arguments, if any, not
// including the debug command itself; args
// are in ASCII and if intended to be numeric
// have to be converted to int. tokens[n] is
// the nth argument after 'debug' 
// 
// --------------------------------------------
int debug(int arg)
{
    if ( arg == 0 )
    {
        terminalOut((char *) "Debug commands are:");
        terminalOut((char *) "\tscan ... I2C bus scanner");
        terminalOut((char *) "\treset .. Reset board");
        terminalOut((char *) "\tdump ... Dump EEPROM");

        // add new command help here
        // NOTE: debug stuff is not part of CLI so
        // the built-in CLI help doesn't apply
        return(0);
    }

    if ( strcmp(tokens[1], "scan") == 0 )
      debug_scan();
    else if ( strcmp(tokens[1], "reset") == 0 )
      debug_reset();
    else if ( strcmp(tokens[1], "dump") == 0 )
      debug_dump_eeprom();

    // add new commands here

    else
    {
      terminalOut((char *) "Invalid debug command");
      return(1);
    }

    return(0);
}
