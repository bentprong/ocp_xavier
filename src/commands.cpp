//===================================================================
// commands.cpp
//
// Functions specific to an OCP project - these are the command
// functions called by the CLI.  Also contains pin definitions that
// are used by the pins, read and write commands. See also main.hpp
// for the actual Arduino pin numbering that aligns with variants.cpp
// README.md in platformio folder of repo has details about this file.
//===================================================================
#include "INA219.h"
#include "main.hpp"
#include "eeprom.hpp"
#include <math.h>

extern char             *tokens[];
extern EEPROM_data_t    EEPROMData;

// pin defs used for 1) pin init and 2) copied into volatile status structure
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

uint16_t      static_pin_count = sizeof(staticPins) / sizeof(pin_mgt_t);

// INA219 defines
// FIXME See GitHub Issue #1 (Current values are incorrect: need values for INA219 setup)
// NOTE: These values were imported from the INA219 Library example code
#define U2_SHUNT_MAX_V  0.04      /* Rated max for our shunt is 75mv for 50 A current: */
                                  /* we will measure only up to 20A so max is about 75mV*20/50 */
#define U2_BUS_MAX_V    16.0      /* with 12v lead acid battery this should be enough*/
#define U2_MAX_CURRENT  3.0       /* In our case this is enaugh even tho shunt is capable to 50 A*/
#define U3_SHUNT_MAX_V  0.04      /* Rated max for our shunt is 75mv for 50 A current: */
                                  /* we will mesaure only up to 20A so max is about 75mV*20/50 */
#define U3_BUS_MAX_V    16.0      /* with 12v lead acid battery this should be enough*/
#define U3_MAX_CURRENT  3.0       /* In our case this is enaugh even tho shunt is capable to 50 A*/
#define SHUNT_R         0.01      /* Shunt resistor in ohms (R211 and R210 are the same ohms) */

#define STATUS_DISPLAY_DELAY_ms     3000

static char             outBfr[OUTBFR_SIZE];
uint8_t                 pinStates[PINS_COUNT] = {0};

// INA219 stuff ('Un' is chip ID on schematic)
INA219::t_i2caddr   u2 = INA219::t_i2caddr(64);
INA219::t_i2caddr   u3 = INA219::t_i2caddr(65);
INA219              u2Monitor(u2);
INA219              u3Monitor(u3);

// --------------------------------------------
// configureIOPins()
// --------------------------------------------
void configureIOPins(void)
{
  pin_size_t        pinNo;

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
}

// --------------------------------------------
// monitorsInit() - initialize current monitors
// --------------------------------------------
void monitorsInit(void)
{
  // NOTE: 'uN' is the chip ID on the schematic
  u2Monitor.begin();
  u2Monitor.configure(INA219::RANGE_16V, INA219::GAIN_8_320MV, INA219::ADC_16SAMP, INA219::ADC_16SAMP, INA219::CONT_SH_BUS);
  u2Monitor.calibrate(SHUNT_R, U2_SHUNT_MAX_V, U2_BUS_MAX_V, U2_MAX_CURRENT);

  u3Monitor.begin();
  u3Monitor.configure(INA219::RANGE_16V, INA219::GAIN_8_320MV, INA219::ADC_16SAMP, INA219::ADC_16SAMP, INA219::CONT_SH_BUS);
  u3Monitor.calibrate(SHUNT_R, U3_SHUNT_MAX_V, U3_BUS_MAX_V, U3_MAX_CURRENT);
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
    uint8_t       pinNo = atoi(tokens[1]);

    if ( pinNo > PINS_COUNT )
    {
        terminalOut((char *) "Invalid pin number; please use Arduino numbering");
        return(1);
    }

    // TODO alternative is bitRead() but it requires a port, so that
    // would have to be extracted from the pin map
    // digitalPinToPort(pin) yields port # if pin is Arduino style
    pin = digitalRead((pin_size_t) pinNo);
    pinStates[pinNo] = pin;
    sprintf(outBfr, "Pin %d (%s) = %d", pinNo, getPinName(pinNo), pin);
    terminalOut(outBfr);
    return(0);
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
        terminalOut((char *) "Invalid pin number; use 'pins' command for help.");
        return(1);
    }    

    if ( staticPins[index].pinFunc != OUTPUT_PIN )
    {
        terminalOut((char *) "Cannot write to an input pin! Use 'pins' command for help.");
        return(1);
    }  

    if ( value != 0 && value != 1 )
    {
        terminalOut((char *) "Invalid pin value; please enter either 0 or 1");
        return(1);
    }

    digitalWrite(pinNo, value);
    pinStates[pinNo] = (bool) value;
    sprintf(outBfr, "Wrote %d to pin # %d (%s)", value, pinNo, getPinName(pinNo));
    terminalOut(outBfr);
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
    float           v12I, v12V, v3p3I, v3p3V;

    terminalOut((char *) "Acquiring current data, please wait...");

    get12VData(&v12I, &v12V);
    get3P3VData(&v3p3I, &v3p3V);
    delay(100);

    sprintf(outBfr, "12V shunt current:  %d mA", (int) v12I);
    terminalOut(outBfr);

    sprintf(outBfr, "12V bus voltage:    %5.2f V", v12V);
    terminalOut(outBfr);

    sprintf(outBfr, "3.3V shunt current: %d mA", (int) v3p3I);
    terminalOut(outBfr);

    sprintf(outBfr, "3.3V bus voltage:   %5.2f V", v3p3V);
    terminalOut(outBfr);  

    return(0);

} // curCmd()

// --------------------------------------------
// pinCmd() - dump all I/O pins on terminal
// --------------------------------------------
int pinCmd(int arg)
{
    int         count = static_pin_count;
    int         index = 0;
    uint8_t     pinNo;

    terminalOut((char *) " #           Pin Name   I/O              #        Pin Name      I/O ");
    terminalOut((char *) "-------------------------------------------------------------------- ");

    while ( count > 0 )
    {
      if ( count == 1 )
      {
          sprintf(outBfr, "%2d %20s %c %d ", staticPins[index].pinNo, staticPins[index].name,
                  staticPins[index].pinFunc == INPUT ? 'I' : 'O', pinStates[staticPins[index].pinNo]);
          terminalOut(outBfr);
          break;
      }
      else
      {
          pinNo = staticPins[index].pinNo;
          sprintf(outBfr, "%2d %20s %c %d\t\t%2d %20s %c %d ", 
                  pinNo, staticPins[index].name, staticPins[index].pinFunc == INPUT ? 'I' : 'O', pinStates[pinNo],
                  staticPins[index+1].pinNo, staticPins[index+1].name, staticPins[index+1].pinFunc == INPUT ? 'I' : 'O',
                  pinStates[staticPins[index+1].pinNo]);
          terminalOut(outBfr);
          count -= 2;
          index += 2;
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
// readAllInputPins() 
// --------------------------------------------
void readAllInputPins(void)
{
    uint8_t             pinNo;

    // NOTE: Outputs are latched after the last write or are 0
    // from reset.
    for ( int i = 0; i < static_pin_count; i++ )
    {
        pinNo = staticPins[i].pinNo;
        if ( staticPins[i].pinFunc == INPUT_PIN )
        {
            pinStates[pinNo] = digitalRead(pinNo);
        }
    }
}

// --------------------------------------------
// statusCmd() - status display (real-time)
// can be stopped by pressing any key
// --------------------------------------------
int statusCmd(int arg)
{
    float             v12I, v12V, v3p3I, v3p3V;

    while ( 1 )
    {
      // get voltages and currents
      get12VData(&v12I, &v12V);
      get3P3VData(&v3p3I, &v3p3V);

      readAllInputPins();

      CLR_SCREEN();
      CURSOR(1, 29);
      displayLine((char *) "Xavier Status Display");

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
      sprintf(outBfr, "12V: %5.2f %d  mA", v12V, (int) v12I);
      displayLine(outBfr);

      CURSOR(11,55);
      sprintf(outBfr, "3.3V: %5.2f %d mA", v3p3V, (int) v3p3I);
      displayLine(outBfr);

      CURSOR(24, 22);
      displayLine((char *) "Hit any key to exit this display");

      if ( SerialUSB.available()  )
      {
        // flush any user input and exit
        while ( SerialUSB.available() )
            (void) SerialUSB.read();

        CLR_SCREEN();
        return(0);
      }

      delay(EEPROMData.status_delay_secs * 1000);
    }

    return(0);

} // statusCmd()


// --------------------------------------------
// getPinName() - get name of pin from pin #
// Returns pointer to pin name, or 'unknown' 
// if pinNo not found.
// --------------------------------------------
const char *getPinName(int pinNo)
{
    for ( int i = 0; i < static_pin_count; i++ )
    {
        if ( pinNo == staticPins[i].pinNo )
            return(staticPins[i].name);
    }

    return("Unknown");
}

// --------------------------------------------
// getPinIndex() - get index into staticPins[]
// based on Arduino pin #. Returns -1 if pin
// not found, else index.
// --------------------------------------------
int8_t getPinIndex(uint8_t pinNo)
{
    for ( int i = 0; i < static_pin_count; i++ )
    {
        if ( staticPins[i].pinNo == pinNo )
            return(i);
    }

    return(-1);
}

void set_help(void)
{
    terminalOut((char *) "EEPROM Parameters are:");
    terminalOut((char *) "  sdelay <int> ........ status display delay in seconds");

    // TODO add more set command help here
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
    String        valueEntered = tokens[2];
    float         fValue;
    int           iValue;
    bool          isDirty = false;

    if ( arg == 0 )
    {
        set_help();
        return(0);
    }

    if ( strcmp(parameter, "sdelay") == 0 )
    {
        // set ADC gain error
        iValue = valueEntered.toInt();
        if (EEPROMData.status_delay_secs != iValue )
        {
          isDirty = true;
          EEPROMData.status_delay_secs = iValue;
        }
    }
    else
    {
        terminalOut("Invalid parameter name");
        set_help();
        return(1);
    }

    if ( isDirty )
        EEPROM_Save();

    return(0);

} // setCmd()
