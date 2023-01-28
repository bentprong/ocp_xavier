#include <Arduino.h>
#include "Wire.h"
#include "float.h"
#include "FlashAsEEPROM_SAMD.h"
#include "INA219.h"

#define FAST_BLINK_DELAY            200
#define SLOW_BLINK_DELAY            1000
#define CMD_NAME_MAX                12
#define MAX_LINE_SZ                 80
#define OUTBFR_SIZE                 (MAX_LINE_SZ * 3)

// disable EEPROM/FLASH library driver debug, because it uses Serial 
// not SerialUSB and may hang on startup as a result
#define FLASH_DEBUG               1

// possible CLI errors
#define CLI_ERR_NO_ERROR          0
#define CLI_ERR_CMD_NOT_FOUND     1
#define CLI_ERR_TOO_FEW_ARGS      2
#define CLI_ERR_TOO_MANY_ARGS     3
#define MAX_TOKENS                8

// INA219 defines
#define U2_SHUNT_MAX_V  0.04      /* Rated max for our shunt is 75mv for 50 A current: 
                                  /* we will measure only up to 20A so max is about 75mV*20/50 */
#define U2_BUS_MAX_V    16.0      /* with 12v lead acid battery this should be enough*/
#define U2_MAX_CURRENT  3.0       /* In our case this is enaugh even tho shunt is capable to 50 A*/
#define U3_SHUNT_MAX_V  0.04      /* Rated max for our shunt is 75mv for 50 A current: 
                                  /* we will mesaure only up to 20A so max is about 75mV*20/50 */
#define U3_BUS_MAX_V    16.0      /* with 12v lead acid battery this should be enough*/
#define U3_MAX_CURRENT  3.0       /* In our case this is enaugh even tho shunt is capable to 50 A*/
#define SHUNT_R         0.01      /* Shunt resistor in ohms (R211 and R210 are the same ohms) */

// I/O Pins (using Arduino scheme, see variant.c the spacing between defines aligns with the
// comments in that file for readability purposes
#define OCP_SCAN_LD_N         0   // PA22
#define OCP_MAIN_PWR_EN       1   // PA23
#define OCP_SCAN_DATA_IN      2   // PA10
#define OCP_SCAN_CLK          3   // PA11

#define OCP_PRSNTB1_N         4   // PB10
#define PCIE_PRES_N           5   // PB11
#define UART_RX_UNUSED        6   // PA20
#define SCAN_VER_0            7   // PA21

#define OCP_SCAN_DATA_OUT     8   // PA08
#define OCP_AUX_PWR_EN        9   // PA09
#define UART_TX_UNUSED        10  // PA19

#define MCU_SDA               11  // PA16
#define MCU_SCL               12  // PA17

// NOTE: LED_PIN 13 PB23 defined in variant.h
#define OCP_PWRBRK_N          14  // PB22

#define OCP_BIF0_N            15  // PA02
#define OCP_PRSNTB3_N         16  // PB02
#define FAN_ON_AUX            17  // PB08
#define OCP_SMB_RST_N         18  // PB09

#define LFF_PORT0             19  // PA05 (not used)
#define LFF_PORT1             20  // PA06 (not used)
#define LFF_PORT2             21  // PA07 (not used)

// NOTE: USB_HS_DP (22) and USB_HS_DN (23) not available to CLI
#define OCP_PRSNTB0_N         24  // PA18
#define OCP_BIF1_N            25  // PA03

#define OCP_SLOT_ID0          26  // PA12
#define OCP_SLOT_ID1          27  // PA13
#define OCP_PRSNTB2_N         28  // PA14
#define SCAN_VER_1            29  // PA15

#define PHY_RESET_N           30  // PA27
#define RBT_ISOLATE_N         31  // PA28
#define OCP_BIF2_N            32  // PA04
#define OCP_WAKE_N            33  // PB03

#define TEMP_WARN             34  // PA00
#define TEMP_CRIT             35  // PA01

#define INPUT_PIN             INPUT
#define OUTPUT_PIN            OUTPUT

// Version
const char      versString[] = "1.0.0";

// Pin Management structure
typedef struct {
  uint8_t           pinNo;
  uint8_t           pinFunc;
  char              name[20];
  bool              state;
} pin_mgt_t;

// constant pin defs used for 1) pin init and 2) copied into volatile status structure
// to maintain state of inputs pins that get written 3) pin names (nice, right?) ;-)
const pin_mgt_t     staticPins[] = {
  {           OCP_SCAN_LD_N, INPUT,  "OCP_SCAN_LD_N", 0},
  {         OCP_MAIN_PWR_EN, INPUT, "OCP_MAIN_PWR_EN", 0},
  {        OCP_SCAN_DATA_IN, INPUT, "OCP_SCAN_DATA_IN", 0},
  {            OCP_SCAN_CLK, INPUT, "OCP_SCAN_CLK", 0},
  {           OCP_PRSNTB1_N, INPUT, "OCP_PRSNTB1_N", 0},
  {             PCIE_PRES_N, INPUT, "PCIE_PRES_N", 0},
  {              SCAN_VER_0, INPUT, "SCAN_VER_0", 0},
  {       OCP_SCAN_DATA_OUT, OUTPUT, "OCP_SCAN_DATA_OUT", 0},
  {          OCP_AUX_PWR_EN, OUTPUT, "OCP_AUX_PWR_EN", 0},
  {            OCP_PWRBRK_N, OUTPUT, "OCP_PWRBRK_N", 0},
  {              OCP_BIF0_N, INPUT, "OCP_BIF0_N", 0},
  {           OCP_PRSNTB3_N, INPUT, "OCP_PRSNTB3_N", 0},
  {              FAN_ON_AUX, INPUT,  "FAN_ON_AUX", 0},
  {           OCP_SMB_RST_N, OUTPUT, "OCP_SMB_RST_N", 0},
  {           OCP_PRSNTB0_N, OUTPUT, "OCP_PRSNTB0_N", 0},
  {              OCP_BIF1_N, OUTPUT, "OCP_BIF1_N", 0},
  {            OCP_SLOT_ID0, OUTPUT, "OCP_SLOT_ID0", 0},
  {            OCP_SLOT_ID1, OUTPUT, "OCP_SLOT_ID1", 0},
  {           OCP_PRSNTB2_N, OUTPUT, "OCP_PRSNTB2_N", 0},
  {              SCAN_VER_1, INPUT, "SCAN_VER_1", 0},
  {             PHY_RESET_N, OUTPUT, "PHY_RESET_N", 0},
  {           RBT_ISOLATE_N, OUTPUT, "RBT_ISOLATE_N", 0},
  {              OCP_BIF2_N, OUTPUT, "OCP_BIF2_N", 0},
  {              OCP_WAKE_N, OUTPUT, "OCP_WAKE_N", 0},
  {               TEMP_WARN, INPUT,  "TEMP_WARN", 0},
  {               TEMP_CRIT, INPUT,  "TEMP_CRIT", 0},

};

#define STATIC_PIN_CNT        (sizeof(staticPins) / sizeof(pin_mgt_t))

// CLI Command Table structure
typedef struct {
    char        cmd[CMD_NAME_MAX];
    int         (*func) (int x);
    int         argCount;
    char        help1[MAX_LINE_SZ];
    char        help2[MAX_LINE_SZ];

} cli_entry;

// EEPROM data storage struct
typedef struct {
    uint32_t        sig;      // unique EEPROMP signature (see #define)

    // TODO add more data

} EEPROM_data_t;

// Constant Data
const char      hello[] = "Dell Xavier NIC 3.0 Test Board V";
const char      cliPrompt[] = "ltf> ";
const int       promptLen = sizeof(cliPrompt);
const uint32_t  EEPROM_signature = 0xDE110C02;  // "DeLL Open Compute 02 (Xavier)"

// INA219 stuff (Un is chip ID on schematic)
INA219::t_i2caddr   u2 = INA219::t_i2caddr(64);
INA219::t_i2caddr   u3 = INA219::t_i2caddr(65);
INA219              u2Monitor(u2);
INA219              u3Monitor(u3);

// Variable data
char            outBfr[OUTBFR_SIZE];
pin_mgt_t       dynamicPins[STATIC_PIN_CNT];

// FLASH/EEPROM Data buffer
EEPROM_data_t   EEPROMData;

// --------------------------------------------
// Forward function prototypes
// --------------------------------------------
void EEPROM_Save(void);
int waitAnyKey(void);

// prototypes for CLI-called functions
// template is func_name(int) because the int arg is the arg
// count from parsing the command line; the arg tokens are
// global in tokens[] with tokens[0] = command entered and
// arg count does not include the command token
int help(int);
int curCmd(int);
int rawRead(int);
int setCmd(int);
int debug(int);
int readCmd(int);
int writeCmd(int);
int pinCmd(int);

// CLI token stack
char                *tokens[MAX_TOKENS];

// CLI command table
// format is "command", function, required arg count, "help line 1", "help line 2" (2nd line can be NULL)
const cli_entry     cmdTable[] = {
    {"debug",    debug, -1, "Debug functions mostly for developer use.", "'debug reset' resets board; 'debug dump' dumps EEPROM"},
    {"help",       help, 0, "THIS DOES NOT DISPLAY ON PURPOSE", " "},
    {"current",  curCmd, 0, "Read current for 12V and 3.3V rails.", " "},
    {"set",      setCmd, 2, "Sets a stored parameter.", "set k 1.234 sets K constant."},
    {"read",    readCmd, 1, "Read input pin.", "read <pin_number> (Arduino numbering)"},
    {"write",  writeCmd, 2, "Write output pin.", "write <pin_number> <0|1> (Arduino numbering)"},
    {"pins",     pinCmd, 0, "Displays pin names and Arduino numbers", " "},
};

#define CLI_ENTRIES     (sizeof(cmdTable) / sizeof(cli_entry))

void terminalOut(char *msg)
{
    SerialUSB.println(msg);
    SerialUSB.flush();
    delay(50);
}

// --------------------------------------------
// doPrompt() - Write prompt to terminal
// --------------------------------------------
void doPrompt(void)
{
    SerialUSB.write(0x0a);
    SerialUSB.write(0x0d);
    SerialUSB.flush();
    SerialUSB.print(cliPrompt);
    SerialUSB.flush();
}

// --------------------------------------------
// cli() - Command Line Interpreter
// --------------------------------------------
bool cli(char *raw)
{
    bool         rc = false;
    int         len;
    char        *token;
    const char  delim[] = " ";
    int         tokNdx = 0;
    char        input[80];
    int         error = CLI_ERR_CMD_NOT_FOUND;
    int         argCount;

    strcpy(input, (char *) raw);
    len = strlen(input);

    // initial call, should get and save the command as 0th token
    token = strtok(input, delim);
    if ( token == NULL )
    {
      doPrompt();
      return(true);
    }

    tokens[tokNdx++] = token;

    // subsequent calls should parse any space-separated args into tokens[]
    // note that what's stored in tokens[] are string pointers into the
    // input array not the actual string token itself
    while ( (token = (char *) strtok(NULL, delim)) != NULL && tokNdx < MAX_TOKENS )
    {
        tokens[tokNdx++] = token;
    }

    if ( tokNdx >= MAX_TOKENS )
    {
        terminalOut("Too many arguments in command line!");
        doPrompt();
        return(false);
    }

    argCount = tokNdx - 1;

    for ( int i = 0; i < (int) CLI_ENTRIES; i++ )
    {
        if ( strncmp(tokens[0], cmdTable[i].cmd, len) == 0 )
        {
            if ( cmdTable[i].argCount == argCount || cmdTable[i].argCount == -1 )
            {
                // command funcs are passed arg count, tokens are global
                (cmdTable[i].func) (argCount);
                SerialUSB.flush();
                rc = true;
                error = CLI_ERR_NO_ERROR;
                break;
            }
            else if ( cmdTable[i].argCount > argCount )
            {
                error = CLI_ERR_TOO_FEW_ARGS;
                rc = false;
                break;
            }
            else
            {
                error = CLI_ERR_TOO_MANY_ARGS;
                rc = false;
                break;
            }
        }
    }

    if ( rc == false )
    {
        if ( error == CLI_ERR_CMD_NOT_FOUND )
         terminalOut("Invalid command");
        else if ( error == CLI_ERR_TOO_FEW_ARGS )
          terminalOut("Not enough arguments for this command, check help.");
        else if ( error == CLI_ERR_TOO_MANY_ARGS )
          terminalOut("Too many arguments for this command, check help.");
        else
          terminalOut("Unknown parser s/w error");
    }

    doPrompt();
    return(rc);

} // cli()

//===================================================================
//                     DEBUG FUNCTIONS
//===================================================================

// --------------------------------------------
// pinCmd() - dump all I/O pins on terminal
// --------------------------------------------
int pinCmd(int arg)
{
    int         count = STATIC_PIN_CNT;
    int         index = 0;

    while ( count > 0 )
    {
      if ( count == 1 )
      {
          sprintf(outBfr, "%2d %20s %c ", staticPins[index].pinNo, staticPins[index].name,
                  staticPins[index].pinFunc == INPUT ? 'I' : 'O');
          terminalOut(outBfr);
          break;
      }
      else
      {
          sprintf(outBfr, "%2d %20s %c\t\t%2d %20s %c ", 
                  staticPins[index].pinNo, staticPins[index].name, staticPins[index].pinFunc == INPUT ? 'I' : 'O',
                  staticPins[index+1].pinNo, staticPins[index+1].name, staticPins[index+1].pinFunc == INPUT ? 'I' : 'O');
          terminalOut(outBfr);
          count -= 2;
          index += 2;
      }
    }
}

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

  terminalOut ("Scanning I2C bus...");

  for (byte i = 8; i < 120; i++)
  {
    scanCount++;
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0)
    {
      sprintf(outBfr, "Found device at address %d 0x%2X", i, i);
      terminalOut(outBfr);
      count++;
      delay(10);  
    } 
  } 

  sprintf(outBfr, "Scan complete, %d addresses scanned in %d ms", scanCount, millis() - startTime);
  terminalOut(outBfr);

  if ( count )
  {
    sprintf(outBfr, "Found %d I2C device(s)", count);
    terminalOut(outBfr);
  }
  else
  {
    terminalOut("No I2c device found");
  }
}

// --------------------------------------------
// debug_reset() - force board reset
// --------------------------------------------
void debug_reset(void)
{
    terminalOut("Board reset will disconnect USB-serial connection now.");
    terminalOut("Repeat whatever steps you took to connect to the board.");
    delay(1000);
    NVIC_SystemReset();
}

// --------------------------------------------
// debug_dump_eeprom() - Dump EEPROM contents
// --------------------------------------------
void debug_dump_eeprom(void)
{
    terminalOut("EEPROM Contents:");
    sprintf(outBfr, "Signature: %08X", EEPROMData.sig);
    terminalOut(outBfr);
}

// --------------------------------------------
// debug() - Main debug program
// --------------------------------------------
int debug(int arg)
{
    if ( arg == 0 )
    {
        terminalOut("Debug commands are:");
        terminalOut("\tscan ... I2C bus scanner");
        terminalOut("\treset .. Reset board");
        terminalOut("\tdump ... Dump EEPROM");
        return(0);
    }

    if ( strcmp(tokens[1], "scan") == 0 )
      debug_scan();
    else if ( strcmp(tokens[1], "reset") == 0 )
      debug_reset();
    else if ( strcmp(tokens[1], "dump") == 0 )
      debug_dump_eeprom();
    else
      terminalOut("Invalid debug command");

    return(0);
}

//===================================================================
//                          CURRENT Command
//===================================================================
int curCmd(int arg)
{
    float           tempF;

    terminalOut("Acquiring current data, please wait...");

    float v12I = u2Monitor.shuntCurrent() * 1000.0;
    float v12V = u2Monitor.busVoltage();
    float v12Power = u2Monitor.busPower() * 1000.0;

    delay(100);

    float v3p3I = u3Monitor.shuntCurrent() * 1000.0;
    float v3p3V = u3Monitor.busVoltage();
    float v3p3Power = u3Monitor.busPower() * 1000.0;

    sprintf(outBfr, "12V shunt current:  %5.2f mA", v12I);
    terminalOut(outBfr);

    sprintf(outBfr, "12V bus voltage:    %5.2f V", v12V);
    terminalOut(outBfr);

    sprintf(outBfr, "3.3V shunt current: %5.2f mA", v3p3I);
    terminalOut(outBfr);

    sprintf(outBfr, "3.3V bus voltage:   %5.2f V", v3p3V);
    terminalOut(outBfr);  

    return(0);

} // curCmd()

// --------------------------------------------
// waitAnyKey() - wait for any key pressed
//
// WARNING: This is a blocking call!
// --------------------------------------------
int waitAnyKey(void)
{
    int             charIn;

    while ( SerialUSB.available() == 0 )
      ;

    charIn = SerialUSB.read();
    return(charIn);
}

const char *getPinName(int pinNo)
{
    for ( int i = 0; i < STATIC_PIN_CNT; i++ )
    {
        if ( pinNo == staticPins[i].pinNo )
            return(staticPins[i].name);
    }

    return("Unknown");
}

//===================================================================
//                    READ, WRITE COMMANDS
//===================================================================

int readCmd(int arg)
{
    uint8_t       pin;
    int           pinNo = atoi(tokens[1]);

    if ( pinNo > PINS_COUNT )
    {
        terminalOut("Invalid pin number; please use Arduino numbering");
        return(1);
    }

    // TODO alternative is bitRead() but it requires a port, so that
    // would have to be extracted from the pin map
    // digitalPinToPort(pin) yields port # if pin is Arduino style
    pin = digitalRead((pin_size_t) pinNo);
    sprintf(outBfr, "Pin %d (%s) = %d", pinNo, getPinName(pinNo), pin);
    terminalOut(outBfr);
}

int writeCmd(int arg)
{
    uint8_t     pinNo = atoi(tokens[1]);
    uint8_t     value = atoi(tokens[2]);

    if ( pinNo > PINS_COUNT )
    {
        terminalOut("Invalid pin number; please use Arduino numbering");
        return(1);
    }    

    if ( value == 0 || value == 1 )
      ;
    else
    {
        terminalOut("Invalid pin value; please enter 0 or 1");
        return(1);
    }

    digitalWrite(pinNo, value);
    sprintf(outBfr, "Wrote %d to pin # %d (%s)", value, pinNo, getPinName(pinNo));
    terminalOut(outBfr);
}

//===================================================================
//                               HELP Command
//===================================================================
int help(int arg)
{
    sprintf(outBfr, "%s %s", hello, versString);
    terminalOut(outBfr);
    terminalOut("Enter a command then press ENTER. Some commands require arguments, which must");
    terminalOut("be separated from the command and other arguments by a space.");
    terminalOut("Up arrow repeats the last command; backspace or delete erases the last");
    terminalOut("character entered. Commands available are:");

    for ( int i = 0; i < (int) CLI_ENTRIES; i++ )
    {
      if ( strcmp(cmdTable[i].cmd, "help") == 0 )
        continue;

      sprintf(outBfr, "%s\t%s", cmdTable[i].cmd, cmdTable[i].help1);
      terminalOut(outBfr);

      if ( cmdTable[i].help2 != NULL )
      {
        sprintf(outBfr, "\t%s", cmdTable[i].help2);
        terminalOut(outBfr);
      }
    }

    return(0);
}

//===================================================================
//                              SET Command
//
// set <parameter> <value>
// 
// Supported parameters: 
//===================================================================
int setCmd(int arg)
{
    char          *parameter = tokens[1];
    String        userEntry = tokens[2];
    float         fValue;
    int           iValue;
    bool          isDirty = false;
#if 0
    if ( strcmp(parameter, "k") == 0 )
    {
        fValue = userEntry.toFloat();
        if ( EEPROMData.K != fValue )
        {
          EEPROMData.K = fValue;
          isDirty = true;
        }
    }
    else if ( strcmp(parameter, "gain") == 0 )
    {
        // set ADC gain error
        iValue = userEntry.toInt();
        if (EEPROMData.gainError != iValue )
        {
          isDirty = true;
          EEPROMData.gainError = iValue;
        }
    }
    else if ( strcmp(parameter, "offset") == 0 )
    {
        // set ADC offset error
        iValue = userEntry.toInt();
        if (EEPROMData.offsetError != iValue )
        {
          isDirty = true;
          EEPROMData.offsetError = iValue;
        }
    }
    else if ( strcmp(parameter, "corr") == 0 )
    {
        // set ADC corr on|off
        if ( strcmp(tokens[2], "off") == 0 )
        {
          if ( EEPROMData.enCorrection == true )
          {
              EEPROMData.enCorrection = false;
              terminalOut("ADC correction off");
              isDirty = true;
          }
        }
        else if ( strcmp(tokens[2], "on") == 0 )
        {
            if ( EEPROMData.enCorrection == false )
            {
                EEPROMData.enCorrection = true;
                isDirty = true;
            }
        }
        else
        {
          terminalOut("Invalid ADC corr argument: must be 'on' or 'off'");
        }
    }
    else
    {
        terminalOut("Invalid parameter name");
        return(1);
    }
#endif

    if ( isDirty )
        EEPROM_Save();

    return(0);
}

//===================================================================
//                      EEPROM/NVM Stuff
//===================================================================

// --------------------------------------------
// EEPROM_Save() - write EEPROM structure to
// the EEPROM
// --------------------------------------------
void EEPROM_Save(void)
{
    uint8_t         *p = (uint8_t *) &EEPROMData;
    uint16_t        eepromAddr = 0;

    for ( int i = 0; i < (int) sizeof(EEPROM_data_t); i++ )
    {
        EEPROM.write(eepromAddr++, *p++);
    }
    
    EEPROM.commit();
}

// --------------------------------------------
// EEPROM_Read() - Read struct from EEPROM
// --------------------------------------------
void EEPROM_Read(void)
{
    uint8_t         *p = (uint8_t *) &EEPROMData;
    uint16_t        eepromAddr = 0;

    for ( int i = 0; i < (int) sizeof(EEPROM_data_t); i++ )
    {
        *p++ = EEPROM.read(eepromAddr++);
    }
}

// --------------------------------------------
// EEPROM_Defaults() - Set defaults in struct
// --------------------------------------------
void EEPROM_Defaults(void)
{
    EEPROMData.sig = EEPROM_signature;
}

// --------------------------------------------
// EEPROM_InitLocal() - Initialize EEPROM & Data
//
// Returns false if no error, else true
// --------------------------------------------
bool EEPROM_InitLocal(void)
{
    bool          rc = false;

    EEPROM_Read();

    if ( EEPROMData.sig != EEPROM_signature )
    {
      // EEPROM failed: either never been used, or real failure
      // initialize the signature and settings

      // FIXME: When debugging, EEPROM fails every time, but it
      // is OK over resets and power cycles.  
      EEPROM_Defaults();

      // save EEPROM data on the device
      EEPROM_Save();

      rc = true;
      terminalOut("EEPROM validation FAILED, EEPROM initialized OK");
    }
    else
    {
      terminalOut("EEPROM validated OK");
    }

    return(rc);

} // EEPROM_InitLocal()

//===================================================================
//                      setup() - Initialization
//===================================================================
void setup() 
{
  bool        LEDstate = false;
  pin_size_t  pinNo;

  // configure all I/O pins

  // configure heartbeat LED pin and turn on which indicates that the
  // board is being initialized
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LEDstate);

  // configure I/O pins
  for ( int i = 0; i < STATIC_PIN_CNT; i++ )
  {
      pinNo = staticPins[i].pinNo;
      pinMode(pinNo, staticPins[i].pinFunc);

      // increase drive strength on output pins
      if ( staticPins[i].pinFunc == OUTPUT )
      {
          // see xavier/variants.cpp for the data in g_APinDescription[]
          // source 7mA, sink 10mA
          PORT->Group[g_APinDescription[pinNo].ulPort].PINCFG[g_APinDescription[pinNo].ulPin].bit.DRVSTR = 1;
      }
  }
  
  // start serial over USB and wait for a connection
  // NOTE: Baud rate isn't applicable to USB but...
  // NOTE: Many libraries won't init unless Serial
  // is running (or in this case SerialUSB)
  SerialUSB.begin(115200);
  while ( !SerialUSB )
  {
      // fast blink while waiting for a connection
      LEDstate = LEDstate ? 0 : 1;
      digitalWrite(PIN_LED, LEDstate);
      delay(FAST_BLINK_DELAY);
  }

  // initialize system & libraries
  EEPROM_InitLocal();

  u2Monitor.begin();
  u2Monitor.configure(INA219::RANGE_16V, INA219::GAIN_8_320MV, INA219::ADC_16SAMP, INA219::ADC_16SAMP, INA219::CONT_SH_BUS);
  u2Monitor.calibrate(SHUNT_R, U2_SHUNT_MAX_V, U2_BUS_MAX_V, U2_MAX_CURRENT);

  u3Monitor.begin();
  u3Monitor.configure(INA219::RANGE_16V, INA219::GAIN_8_320MV, INA219::ADC_16SAMP, INA219::ADC_16SAMP, INA219::CONT_SH_BUS);
  u3Monitor.calibrate(SHUNT_R, U3_SHUNT_MAX_V, U3_BUS_MAX_V, U3_MAX_CURRENT);

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
  static char     lastCmd[80] = {0};
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