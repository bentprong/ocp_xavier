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
//#define FLASH_DEBUG               1

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
//#define OCP_SCAN_CLK          3   // PA11 -- clock output is not accessible by CLI

#define OCP_PRSNTB1_N         4   // PB10
#define PCIE_PRES_N           5   // PB11
#define UART_TX_UNUSED        6   // PA20
#define SCAN_VER_0            7   // PA21

#define OCP_SCAN_DATA_OUT     8   // PA08
#define OCP_AUX_PWR_EN        9   // PA09
#define NIC_PWR_GOOD          10  // PA19 ** NOTE ** P12/1 jumpered to P_UART1/3

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
#define RBT_ISOLATE_EN        31  // PA28
#define OCP_BIF2_N            32  // PA04
#define OCP_WAKE_N            33  // PB03

#define TEMP_WARN             34  // PA00
#define TEMP_CRIT             35  // PA01

// alises to support pin_mgt_t pinFunc - because Arduino doesn't
// "like" writing to an input pin
#define INPUT_PIN             INPUT
#define OUTPUT_PIN            OUTPUT
#define IN_OUT_PIN            4             // reserved future when DIP switches won't short

// Version
const char      versString[] = "1.0.0";

// Pin Management structure
typedef struct {
  uint8_t           pinNo;
  uint8_t           pinFunc;
  char              name[20];
} pin_mgt_t;

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
const char      cliPrompt[] = "cmd> ";
const int       promptLen = sizeof(cliPrompt);
const uint32_t  EEPROM_signature = 0xDE110C02;  // "DeLL Open Compute 02 (Xavier)"
const uint8_t   eepromAddresses[4] = {0, 0x52, 0, 0x56};      // NOTE: these DO NOT match Table 67

// constant pin defs used for 1) pin init and 2) copied into volatile status structure
// to maintain state of inputs pins that get written 3) pin names (nice, right?) ;-)
// NOTE: Any I/O that is connected to the DIP switches HAS to be an input because those
// switches can be strapped to ground.  Thus, if the pin was an output and a 1 was
// written, there would be a dead short on that pin (no resistors).
// NOTE: The order of the entries in this table is the order they are displayed by the
// 'pins' command. There is no other signficance to the order.
const pin_mgt_t     staticPins[] = {
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

#define STATIC_PIN_CNT        (sizeof(staticPins) / sizeof(pin_mgt_t))

// INA219 stuff (Un is chip ID on schematic)
INA219::t_i2caddr   u2 = INA219::t_i2caddr(64);
INA219::t_i2caddr   u3 = INA219::t_i2caddr(65);
INA219              u2Monitor(u2);
INA219              u3Monitor(u3);

// Variable data
char                outBfr[OUTBFR_SIZE];
uint8_t             pinStates[PINS_COUNT] = {0};

// FLASH/EEPROM Data buffer
EEPROM_data_t       EEPROMData;

// --------------------------------------------
// Forward function prototypes
// --------------------------------------------
void EEPROM_Save(void);
int waitAnyKey(void);
void terminalOut(char *msg);

// ANSI terminal escape sequence
#define CLR_SCREEN()                terminalOut("\x1b[2J")
#define CLR_LINE()                  terminalOut("\x1b[0K")
#define SHOW()                      terminalOut(outBfr)

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
int statusCmd(int);
int eepromCmd(int);

// CLI token stack
char                *tokens[MAX_TOKENS];

// CLI command table
// format is "command", function, required arg count, "help line 1", "help line 2" 
const cli_entry     cmdTable[] = {
    {"debug",      debug,  -1, "Debug functions mostly for developer use.",      "Enter 'debug' with no arguments for help."},
    {"help",        help,   0, "NOTE: THIS DOES NOT DISPLAY ON PURPOSE",         " "},
    {"current",   curCmd,   0, "Read current for 12V and 3.3V rails.",           " "},
//  {"set",       setCmd,   2, "Sets a parameter in EEPROM.",                    "set <param> <value>"},
    {"read",     readCmd,   1, "Read input pin (Arduino numbering).",            "read <pin_number>"},
    {"write",   writeCmd,   2, "Write output pin (Arduino numbering).",          "write <pin_number> <0|1>"},
    {"pins",      pinCmd,   0, "Displays pin names and numbers.",                "Xavier uses Arduino-style pin numbering."},
    {"status", statusCmd,   0, "Displays status of I/O pins etc.",               " "},
    {"eeprom", eepromCmd,   0, "Displays contents of FRU EEPROM.",               "FRU EEPROM is on the inserted NIC 3.0 card."},
};

#define CLI_ENTRIES     (sizeof(cmdTable) / sizeof(cli_entry))

// --------------------------------------------
// CURSOR() - position cursor at (r,c) on ANSI
// terminal.  Re-written from macro to flush
// the stream and delay slightly.
// --------------------------------------------
void CURSOR(uint8_t r,uint8_t c)                 
{
    char          bfr[12];

    sprintf(bfr, "\x1b[%d;%df", r, c);
    SerialUSB.write(bfr);
    SerialUSB.flush();
    delay(5);
}

// --------------------------------------------
// terminalOut() - wrapper to Serial.println
// that flushes the stream and delays slightly
// --------------------------------------------
void terminalOut(char *msg)
{
    SerialUSB.println(msg);
    SerialUSB.flush();
    delay(50);
}

// --------------------------------------------
// displayLine() - wrapper to Serial.write()
// to flush the stream and delay slightly
// --------------------------------------------
void displayLine(char *m)
{
    SerialUSB.write(m);
    SerialUSB.flush();
    delay(10);
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

    terminalOut(" #           Pin Name   I/O              #        Pin Name     I/O");
    terminalOut("------------------------------------------------------------------");

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
  char        *s;

  terminalOut ("Scanning I2C bus...");

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
        terminalOut("Debug commands are:");
        terminalOut("\tscan ... I2C bus scanner");
        terminalOut("\treset .. Reset board");
        terminalOut("\tdump ... Dump EEPROM");

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
      terminalOut("Invalid debug command");
      return(1);
    }

    return(0);
}

//===================================================================
//                          CURRENT Command
//===================================================================

// --------------------------------------------
// get12VData() - get 12V rail V & I
// --------------------------------------------
void get12VData(float *v12I, float *v12V)
{
    *v12I = u2Monitor.shuntCurrent() * 1000.0;
    *v12V = u2Monitor.busVoltage();
}

// --------------------------------------------
// get 3P3VData() - get 3.3V rail V & I
// --------------------------------------------
void get3P3VData(float *v3p3I, float *v3p3V)
{
    *v3p3I = u3Monitor.shuntCurrent() * 1000.0;
    *v3p3V = u3Monitor.busVoltage();    
}

// --------------------------------------------
// curCmd() - display 12V and 3.3V V & I
// --------------------------------------------
int curCmd(int arg)
{
    float           tempF, v12I, v12V, v3p3I, v3p3V;

    terminalOut("Acquiring current data, please wait...");

    get12VData(&v12I, &v12V);
    get3P3VData(&v3p3I, &v3p3V);

//    float v12Power = u2Monitor.busPower() * 1000.0;

    delay(100);


//    float v3p3Power = u3Monitor.busPower() * 1000.0;

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

// --------------------------------------------
// getPinName() - get name of pin from pin #
// --------------------------------------------
const char *getPinName(int pinNo)
{
    for ( int i = 0; i < STATIC_PIN_CNT; i++ )
    {
        if ( pinNo == staticPins[i].pinNo )
            return(staticPins[i].name);
    }

    return("Unknown");
}

// --------------------------------------------
// getPinIndex() - get index into staticPins[]
// based on Arduino pin #
// --------------------------------------------
int8_t getPinIndex(uint8_t pinNo)
{
    for ( int i = 0; i < STATIC_PIN_CNT; i++ )
    {
        if ( staticPins[i].pinNo == pinNo )
            return(i);
    }

    return(-1);
}

//===================================================================
//                    READ, WRITE COMMANDS
//===================================================================

// --------------------------------------------
// readCmd() - read pin
// --------------------------------------------
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

// --------------------------------------------
// writeCmd() - write a pin with 0 or 1
// --------------------------------------------
int writeCmd(int arg)
{
    uint8_t     pinNo = atoi(tokens[1]);
    uint8_t     value = atoi(tokens[2]);
    uint8_t     index = getPinIndex(pinNo);

    if ( pinNo > PINS_COUNT || index == -1 )
    {
        terminalOut("Invalid pin number; use 'pins' command for help.");
        return(1);
    }    

    if ( staticPins[index].pinFunc != OUTPUT_PIN )
    {
        terminalOut("Cannot write to an input pin! Use 'pins' command for help.");
        return(1);
    }  

    if ( value == 0 || value == 1 )
      ;
    else
    {
        terminalOut("Invalid pin value; please enter either 0 or 1");
        return(1);
    }

    digitalWrite(pinNo, value);
    pinStates[pinNo] = (bool) value;
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
    terminalOut(" ");

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
//                         Status Display Screen
//===================================================================

// --------------------------------------------
// padBuffer() - pad a buffer with spaces
// --------------------------------------------
 char *padBuffer(int pos)
 {
    int         leftLen = strlen(outBfr);
    int         padLen = pos - leftLen;
    char        *s;

    s = &outBfr[leftLen];
    memset((void *) s, ' ', padLen);
    s += padLen;
    return(s);
 }

// --------------------------------------------
// statusCmd() - status display
// --------------------------------------------
int statusCmd(int arg)
{
    uint8_t           temp = 0;
    char              rightBfr[42];
    int               leftLen, padLen;
    char              *s;
    uint8_t           pinNo;
    float             v12I, v12V, v3p3I, v3p3V;

    while ( 1 )
    {
      // get voltages and currents
      get12VData(&v12I, &v12V);
      get3P3VData(&v3p3I, &v3p3V);

      // read all input pins
      // NOTE: Outputs are latched after the last write or are 0
      // from reset.
      for ( int i = 0; i < STATIC_PIN_CNT; i++ )
      {
          pinNo = staticPins[i].pinNo;
          if ( staticPins[i].pinFunc == INPUT_PIN )
          {
              pinStates[pinNo] = digitalRead(pinNo);
          }
      }

      // clear display
      CLR_SCREEN();
      CURSOR(1, 29);
      displayLine("Xavier Status Display");

      CURSOR(3,1);
      sprintf(outBfr, "TEMP WARN         %d", pinStates[TEMP_WARN]);
      displayLine(outBfr);

      CURSOR(3,57);
      sprintf(outBfr, "BIF [2:0]      %u%u%u", pinStates[OCP_BIF2_N], pinStates[OCP_BIF1_N], pinStates[OCP_BIF0_N]);
      displayLine(outBfr);

      CURSOR(4,1);
      sprintf(outBfr, "TEMP CRIT         %u", pinStates[TEMP_CRIT]);
      displayLine(outBfr);

      CURSOR(4,54);
      sprintf(outBfr, "PRSNTB [3:0]     %u%u%u%u", pinStates[OCP_PRSNTB3_N], pinStates[OCP_PRSNTB2_N], pinStates[OCP_PRSNTB1_N], pinStates[OCP_PRSNTB0_N]);
      displayLine(outBfr);

      CURSOR(5,1);
      sprintf(outBfr, "FAN ON AUX        %u", pinStates[FAN_ON_AUX]);
      displayLine(outBfr);

      CURSOR(5,53);
      sprintf(outBfr, "SLOT ID [1:0]       %u%u", pinStates[OCP_SLOT_ID1], pinStates[OCP_SLOT_ID0]);
      displayLine(outBfr);

      CURSOR(6,1);
      sprintf(outBfr, "SCAN_LD_N         %d", pinStates[OCP_SCAN_LD_N]);
      displayLine(outBfr);

      CURSOR(6,51);
      sprintf(outBfr, "SCAN VERS [1:0]       %u%u", pinStates[SCAN_VER_1], pinStates[SCAN_VER_0]);
      displayLine(outBfr);

      CURSOR(7,1);
      sprintf(outBfr, "AUX_PWR_EN        %d", pinStates[OCP_AUX_PWR_EN]);
      displayLine(outBfr);      

      CURSOR(7,56);
      sprintf(outBfr, "PCIE_PRES_N       %d", pinStates[PCIE_PRES_N]);
      displayLine(outBfr);

      CURSOR(8,1);
      sprintf(outBfr, "MAIN_PWR_EN       %d", pinStates[OCP_MAIN_PWR_EN]);
      displayLine(outBfr);  

      CURSOR(8,58);
      sprintf(outBfr, "OCP_WAKE_N      %d", pinStates[OCP_WAKE_N]);
      displayLine(outBfr);

      CURSOR(9,1);
      sprintf(outBfr, "RBT_ISOLATE_EN    %d", pinStates[RBT_ISOLATE_EN]);
      displayLine(outBfr);  

      CURSOR(9,57);
      sprintf(outBfr, "OCP_PWRBRK_N     %d", pinStates[OCP_PWRBRK_N]);
      displayLine(outBfr);

      CURSOR(10,1);
      sprintf(outBfr, "jmp_NIC_PWR_GOOD  %d", pinStates[NIC_PWR_GOOD]);
      displayLine(outBfr);  

      CURSOR(11,1);
      sprintf(outBfr, "12V: %5.2f %5.2f  mA", v12V, v12I);
      displayLine(outBfr);

      CURSOR(11,55);
      sprintf(outBfr, "3.3V: %5.2f %5.2f mA", v3p3V, v3p3I);
      displayLine(outBfr);

      // info line & check for any key pressed
      CURSOR(24, 22);
      displayLine("ESC to exit; ENTER for next page");

      if ( SerialUSB.available()  )
      {
        (void) SerialUSB.read();
        CLR_SCREEN();
        return(0);
      }

      delay(3000);
    }

} // statusCmd()



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

#define MAX_I2C_WRITE     16        // safe size, could be larger?
#define EEPROM_MAX_LEN    128

// temporary read buffer for FRU EEPROM
byte              EEPROMBuffer[EEPROM_MAX_LEN];

// --------------------------------------------
// readEEPROM() - read from FRU EEPROM into
// EEPROMBuffer up to max length
// --------------------------------------------
void readEEPROM(uint8_t i2cAddr, uint32_t eeaddress, byte *dest, uint16_t length)
{
  uint16_t          index = 0;

  if ( length > EEPROM_MAX_LEN )
    length = EEPROM_MAX_LEN;

  Wire.beginTransmission(i2cAddr);

  Wire.write((int)(eeaddress >> 8));      // MSB
  Wire.write((int)(eeaddress & 0xFF));    // LSB
  Wire.endTransmission();

  Wire.requestFrom(i2cAddr, length);

  while ( Wire.available() && length-- > 0 ) 
  {
      *dest++  = Wire.read();
  }
}

// --------------------------------------------
// writeEEPROMPage() - write a 'page' to the
// FRU EEPROM. NOTE: Not in use, here in case.
// NOTE: Not tested! Should work, hahah.
// --------------------------------------------
void writeEEPROMPage(uint8_t i2cAddr, long eeAddress, byte *buffer)
{

  Wire.beginTransmission(i2cAddr);

  Wire.write((int)(eeAddress >> 8));        // MSB
  Wire.write((int)(eeAddress & 0xFF));      // LSB

  // Write bytes to EEPROM
  for (byte x = 0 ; x < MAX_I2C_WRITE ; x++)
    Wire.write(buffer[x]);                //Write the data

  Wire.endTransmission();                 //Send stop condition
}

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

// Table 8-1 COMMON HEADER
typedef struct {
  uint8_t         format_vers;
  uint8_t         internal_area_offset;
  uint8_t         chassis_area_offset;
  uint8_t         board_area_offset;
  uint8_t         product_area_offset;
  uint8_t         multirecord_area_offset;
  uint8_t         pad;
  uint8_t         cksum;
} common_hdr_t;

typedef struct {
  uint8_t         format_vers;
  uint8_t         board_area_length;
  uint8_t         language;
  uint8_t         mfg_time[3];           // mins since 0:00 1/1/1996 little endian
  uint8_t         manuf_type_length;
} board_hdr_t;

typedef struct {
  uint16_t        board_area_offset_actual;
  uint16_t        board_area_length;
  uint16_t        product_area_offset_actual;
  uint16_t        internal_area_offset_actual;
  uint16_t        chassis_area_offset_actual;
  uint16_t        multirecord_area_offset_actual;
} eeprom_desc_t;

#define TYPE_LENGTH_MASK        0x3F
#define GET_TYPE(x)             (x >> 6)
#define GET_LENGTH(x)           (x & TYPE_LENGTH_MASK)

common_hdr_t      commonHeader;
board_hdr_t       boardHeader;
eeprom_desc_t     EEPROMDescriptor;
const char        sixBitASCII[64] = " !\"$%&'()*+,-./123456789:;<=>?ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_";

void unpack6bitASCII(char *s, uint8_t field_length, uint8_t *bytes)
{
    uint8_t         temp;
    uint16_t        field_offset = 0;

    while ( field_length > 0 )
    {
        temp = bytes[field_offset] & 0x3F;
        *s++ = sixBitASCII[temp];

        temp = bytes[field_offset + 1] << 2 | (bytes[field_offset] >> 6);
        *s++ = sixBitASCII[temp];

        temp = bytes[field_offset + 1] >> 4 | bytes[field_offset + 2] & 3;
        *s++ = sixBitASCII[temp];

        *s++ = sixBitASCII[bytes[field_offset + 2] >> 2];

        field_length -= 3;
        field_offset += 3;
    }

    *s = 0;
}

uint16_t extractField(char *t, uint8_t type_length, uint16_t field_offset)
{
    uint8_t field_type = GET_TYPE(type_length);
    uint16_t field_length = GET_LENGTH(type_length);

    if ( field_type == 3 )
    {
        // 8-bit ASCII
        strncpy(t, (char *) &EEPROMBuffer[field_offset], field_length);
        t[field_length] = 0;
    }
    else if ( field_type == 2 )
    {
        // 6-bit ASCII
        unpack6bitASCII(t, field_length, &EEPROMBuffer[field_offset]);
    }
    else if ( field_type == 1 )
    {
        // BCD plus
        strcpy(t, "BCD plus cannot be parsed");
    }
    else
    {
        // binary or unspecified
        strcpy(t, "binary or unspecified format");
    }

    return(field_offset + field_length);
}

// --------------------------------------------
// eepromCmd() - 'eeprom' command
// --------------------------------------------
int eepromCmd(int arg)
{
    uint16_t          eepromAddr = 0, index;
    uint8_t           eepromI2CAddr = 0x52;
    char              tempStr[256];
    uint8_t           field_type;
    uint16_t          field_offset, field_length;
    uint8_t           slot;
    char              *s;

    // read the slot ID, which determines the FRU EEPROM I2C address
    // NOTE: Default is for slot 1...
    slot = digitalRead(OCP_SLOT_ID1) << 1 | digitalRead(OCP_SLOT_ID0);
    if ( slot >= 0 && slot <= 3 )
      eepromI2CAddr = eepromAddresses[slot];

    // the first byte in the EEPROM should be a 1 which is the format version
    readEEPROM(eepromI2CAddr, 0, EEPROMBuffer, 1);
    if ( EEPROMBuffer[0] != 1 )
    {
        sprintf(outBfr, "Unable to locate FRU EEPROM");
        return(0);
    }

    sprintf(outBfr, "FRU EEPROM found at SMB address 0x%02x", eepromI2CAddr);
    SHOW();

    // read common header
    readEEPROM(eepromI2CAddr, eepromAddr, (byte *) &commonHeader, sizeof(common_hdr_t));
//    dumpMem((unsigned char *) &commonHeader, sizeof(common_hdr_t));

    terminalOut("COMMON HEADER DATA");
    sprintf(outBfr, "Format version %d", commonHeader.format_vers & 0xF);
    SHOW();

    // all area offsets in common area are x8 bytes
    EEPROMDescriptor.internal_area_offset_actual = commonHeader.internal_area_offset * 8;
    EEPROMDescriptor.chassis_area_offset_actual = commonHeader.chassis_area_offset * 8;
    EEPROMDescriptor.board_area_offset_actual = commonHeader.board_area_offset * 8;
    EEPROMDescriptor.product_area_offset_actual = commonHeader.product_area_offset * 8;
    EEPROMDescriptor.multirecord_area_offset_actual = commonHeader.multirecord_area_offset * 8;

    sprintf(outBfr, "Int Use Area:  %d",  EEPROMDescriptor.internal_area_offset_actual);
    SHOW();

    sprintf(outBfr, "Chassis Area:  %d", EEPROMDescriptor.chassis_area_offset_actual);
    SHOW();

    sprintf(outBfr, "Board Area:    %d", EEPROMDescriptor.board_area_offset_actual);
    SHOW();

    sprintf(outBfr, "Product Area:  %d", EEPROMDescriptor.product_area_offset_actual);
    SHOW();

    sprintf(outBfr, "MRecord Area:  %d", EEPROMDescriptor.multirecord_area_offset_actual);
    SHOW();

    // read first 7 bytes of board info area "header" to determine length
    eepromAddr = EEPROMDescriptor.board_area_offset_actual;
    readEEPROM(eepromI2CAddr, eepromAddr, (byte *) &boardHeader, sizeof(board_hdr_t));
//    dumpMem(EEPROMBuffer, sizeof(board_hdr_t));

    EEPROMDescriptor.board_area_length = boardHeader.board_area_length * 8;

    terminalOut("BOARD AREA DATA");
    sprintf(outBfr, "Language Code: %02X", boardHeader.language);
    SHOW();

    sprintf(outBfr, "Mfg Date/Time: %02X%02X%02X", boardHeader.mfg_time[0], boardHeader.mfg_time[1], boardHeader.mfg_time[2]);
    SHOW();

    sprintf(outBfr, "Board Length:  %d", EEPROMDescriptor.board_area_length);
    SHOW();
    
    // read the entire board area
    eepromAddr += sizeof(board_hdr_t);
    readEEPROM(eepromI2CAddr, eepromAddr, (byte *) &EEPROMBuffer, EEPROMDescriptor.board_area_length);
//    dumpMem((unsigned char *) EEPROMBuffer, EEPROMDescriptor.board_area_length);

    field_offset = 0;

    // extract manufacturer name
    field_offset = extractField(tempStr, boardHeader.manuf_type_length, field_offset);
    sprintf(outBfr, "Manufacturer:  %s", tempStr);
    SHOW();

    // extract the next field: product name
    field_offset = extractField(tempStr, EEPROMBuffer[field_offset], ++field_offset);
    sprintf(outBfr, "Product Name:  %s", tempStr);
    SHOW();

    // extract the next field: serial number
    field_offset = extractField(tempStr, EEPROMBuffer[field_offset], ++field_offset);
    sprintf(outBfr, "Serial Number: %s", tempStr);
    SHOW();

    // extract the next field: part #
    field_offset = extractField(tempStr, EEPROMBuffer[field_offset], ++field_offset);
    sprintf(outBfr, "Part Number:   %s", tempStr);
    SHOW();

}

// --------------------------------------------
// EEPROM_Save() - write EEPROM structure to
// the simulated EEPROM
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
// EEPROM_Read() - Read struct from simulated
// EEPROM
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
// NOTE: For simulated EEPROM, not FRU EEPROM
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

  // NOTE: The INA219 driver starts Wire so we don't have to
  // although it is unclear what the speed is when it does it
  //  Wire.begin();
  //  Wire.setClock(400000);

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
  // NOTE: 'u2' is the chip ID on the schematic
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