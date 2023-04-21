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
#include "commands.hpp"

extern char                 *tokens[];
extern EEPROM_data_t        EEPROMData;
extern volatile uint32_t    scanClockPulseCounter;
extern volatile bool        enableScanClk;
extern volatile uint32_t    scanShiftRegister_0;

// pin defs used for 1) pin init and 2) copied into volatile status structure
// to maintain state of inputs pins that get written 3) pin names (nice, right?) ;-)
// NOTE: Any I/O that is connected to the DIP switches HAS to be an input because those
// switches can be strapped to ground.  Thus, if the pin was an output and a 1 was
// written, there would be a dead short on that pin (no resistors).
// NOTE: The order of the entries in this table is the order they are displayed by the
// 'pins' command. There is no other signficance to the order.
 const pin_mgt_t     staticPins[] = {
  {           OCP_SCAN_LD_N, OUTPUT_PIN,  ACT_LO, "OCP_SCAN_LD_N"},
  {            OCP_SCAN_CLK, OUTPUT_PIN,  ACT_LO, "OCP_SCAN_CLK"},
  {         OCP_MAIN_PWR_EN, OUTPUT_PIN,  ACT_HI, "OCP_MAIN_PWR_EN"},
  {        OCP_SCAN_DATA_IN, INPUT,       ACT_HI, "SCAN_DATA_IN"},  // "in" from NIC 3.0 card (baseboard perspective)
  {           OCP_PRSNTB1_N, INPUT_PIN,   ACT_LO, "OCP_PRSNTB1_N"},
  {             PCIE_PRES_N, INPUT_PIN,   ACT_LO, "PCIE_PRES_N"},
  {              SCAN_VER_0, INPUT_PIN,   ACT_HI, "SCAN_VER_0"},
  {       OCP_SCAN_DATA_OUT, OUTPUT,      ACT_HI, "SCAN_DATA_OUT"}, // "out" to NIC 3.0 card
  {          OCP_AUX_PWR_EN, OUTPUT_PIN,  ACT_HI, "OCP_AUX_PWR_EN"},
  {            NIC_PWR_GOOD, INPUT_PIN,   ACT_HI, "jmp_NIC_PWR_GOOD"},  // jumpered see #define for details
  {            OCP_PWRBRK_N, INPUT_PIN,   ACT_LO, "OCP_PWRBRK_N"},
  {              OCP_BIF0_N, INPUT_PIN,   ACT_LO, "OCP_BIF0_N"},
  {           OCP_PRSNTB3_N, INPUT_PIN,   ACT_LO, "OCP_PRSNTB3_N"},
  {              FAN_ON_AUX, INPUT_PIN,   ACT_HI, "FAN_ON_AUX"},
  {           OCP_SMB_RST_N, OUTPUT_PIN,  ACT_LO, "OCP_SMB_RST_N"},
  {           OCP_PRSNTB0_N, INPUT_PIN,   ACT_LO, "OCP_PRSNTB0_N"},
  {              OCP_BIF1_N, INPUT_PIN,   ACT_LO, "OCP_BIF1_N"},
  {            OCP_SLOT_ID0, INPUT_PIN,   ACT_HI, "OCP_SLOT_ID0"},
  {            OCP_SLOT_ID1, INPUT_PIN,   ACT_HI, "OCP_SLOT_ID1"},
  {           OCP_PRSNTB2_N, INPUT_PIN,   ACT_LO, "OCP_PRSNTB2_N"},
  {              SCAN_VER_1, INPUT_PIN,   ACT_HI, "SCAN_VER_1"},
  {             PHY_RESET_N, OUTPUT_PIN,  ACT_LO, "PHY_RESET_N"},
  {          RBT_ISOLATE_EN, OUTPUT_PIN,  ACT_HI, "RBT_ISOLATE_EN"},
  {              OCP_BIF2_N, INPUT_PIN,   ACT_LO, "OCP_BIF2_N"},
  {              OCP_WAKE_N, INPUT_PIN,   ACT_LO, "OCP_WAKE_N"},
  {               TEMP_WARN, INPUT_PIN,   ACT_HI, "TEMP_WARN"},
  {               TEMP_CRIT, INPUT_PIN,   ACT_HI, "TEMP_CRIT"},
};

uint16_t      static_pin_count = sizeof(staticPins) / sizeof(pin_mgt_t);

typedef struct {
    uint8_t     bitNo;
    char        bitName[20];
} scan_data_t;

// NOTE: This table is processed aligning with bits 31..0
// The bitNo entry is NOT used
const scan_data_t     scanBitNames[] = {
    // Byte 0
    {7, "0.7 FAN_ON_AUX"},
    {6, "0.6 TEMP_CRIT_N"},
    {5, "0.5 TEMP_WARN_N"},
    {4, "0.4 WAKE_N"},
    {3, "0.3 PRSNTB[3]_P#"},
    {2, "0.2 PRSNTB[2]_P#"},
    {1, "0.1 PRSNTB[1]_P#"},
    {0, "0.0 PRSNTB[0]_P#"},

    // Byte 1
    {15, "1.7 LINK_SPDB_P2#"},
    {14, "1.6 LINK_SPDA_P2#"},
    {13, "1.5 ACT_P1#"},
    {12, "1.4 LINK_SPDB_P1#"},
    {11, "1.3 LINK_SPDA_P1#"},
    {10, "1.2 ACT_PO#"},
    {9, "1.1 LINK_SPDB_PO#"},
    {8, "1.0 LINK_SPDA_PO#"},

    // Byte 2
    {23, "2.7 LINK_SPDA_P5#"},
    {22, "2.6 ACT_P4#"},
    {21, "2.5 LINK_SPDB_P4#"},
    {20, "2.4 LINK_SPDA_P4#"},
    {19, "2.3 ACT_P3#"},
    {18, "2.2 LINK_SPDB_P3#"},
    {17, "2.1 LINK_SPDA_P3#"},
    {16, "2.0 ACT_P2#"},

    // Byte 3
    {31, "3.7 ACT_P7#"},
    {30, "3.6 LINK_SPDB_P7#"},
    {29, "3.5 LINK_SPDA_P7#"},
    {28, "3.4 ACT_P6#"},
    {27, "3.3 LINK_SPDB_P6#"},
    {26, "3.2 LINK_SPDA_P6#"},
    {25, "3.1 ACT_P5#"},
    {24, "3.0 LINK_SPDB_P5#"},
};
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

// Prototypes
void timers_scanChainCapture(void);
void writePin(uint8_t pinNo, uint8_t value);
void readAllPins(void);

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

      if ( staticPins[i].pinFunc == OUTPUT )
      {
          // increase drive strength on output pins
          // see ttf/variants.cpp for the data in g_APinDescription[]
          // NOTE: this will source 7mA, sink 10mA
          PORT->Group[g_APinDescription[pinNo].ulPort].PINCFG[g_APinDescription[pinNo].ulPin].bit.DRVSTR = 1;

          // deassert pin
          writePin(pinNo, (staticPins[i].activeState == ACT_LO) ? 1 : 0);
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

/**
  * @name   readPin
  * @brief  wrapper to digitalRead via pinStates[]
  * @param  None
  * @retval None
  */
bool readPin(uint8_t pinNo)
{
    uint8_t         index = getPinIndex(pinNo);

    if ( staticPins[index].pinFunc == INPUT )
        pinStates[index] = digitalRead((pin_size_t) pinNo);

    return(pinStates[index]);
}

/**
  * @name   writePin
  * @brief  wrapper to digitalWrite via pinStates[]
  * @param  pinNo = Arduino pin #
  * @param  value = value to write 0 or 1
  * @retval None
  */
void writePin(uint8_t pinNo, uint8_t value)
{
    value = (value == 0) ? 0 : 1;
    digitalWrite(pinNo, value);
    pinStates[getPinIndex(pinNo)] = (bool) value;
}

/**
  * @name   readCmd
  * @brief  read an I/O pin
  * @param  arg 1 = Arduino pin #
  * @retval 0=OK 1=pin # not found
  * @note   displays pin info
  */
int readCmd(int arg)
{
    uint8_t       pinNo = atoi(tokens[1]);
    uint8_t       index = getPinIndex(pinNo);

    if ( isCardPresent() == false )
    {
        terminalOut((char *) "NIC card is not present; cannot read an I/O pin");
        return(1);
    }

    if ( pinNo > PINS_COUNT )
    {
        terminalOut((char *) "Invalid pin number; please use Arduino numbering");
        return(1);
    }

    (void) readPin(pinNo);
    sprintf(outBfr, "%s Pin %d (%s) = %d", (staticPins[index].pinFunc == INPUT) ? "Input" : "Output", 
            pinNo, getPinName(pinNo), pinStates[index]);
    terminalOut(outBfr);
    return(0);
}

/**
  * @name   writeCmd
  * @brief  write a pin with 0 or 1
  * @param  arg 1 Arduino pin #
  * @param  arg 2 value to write
  * @retval None
  */
int writeCmd(int argCnt)
{
    uint8_t     pinNo = atoi(tokens[1]);
    uint8_t     value = atoi(tokens[2]);
    uint8_t     index = getPinIndex(pinNo);

    if ( isCardPresent() == false )
    {
        terminalOut((char *) "NIC card is not present; cannot write an I/O pin");
        return(1);
    }

    if ( pinNo > PINS_COUNT || index == -1 )
    {
        terminalOut((char *) "Invalid pin number; use 'pins' command for help.");
        return(1);
    }    

    if ( staticPins[index].pinFunc == INPUT )
    {
        terminalOut((char *) "Cannot write to an input pin! Use 'pins' command for help.");
        return(1);
    }  

    if ( value != 0 && value != 1 )
    {
        terminalOut((char *) "Invalid pin value; please enter either 0 or 1");
        return(1);
    }

    writePin(pinNo, value);

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
// getPinChar() - get I,O or B pin designator
// --------------------------------------------
char getPinChar(int index)
{
    if ( staticPins[index].pinFunc == INPUT )
        return('<');
    else if ( staticPins[index].pinFunc == OUTPUT )
        return('>');
    else
        return('=');
}

// --------------------------------------------
// pinCmd() - dump all I/O pins on terminal
// --------------------------------------------
int pinCmd(int arg)
{
    int         count = static_pin_count;
    int         index = 0;

    terminalOut((char *) " ");
    terminalOut((char *) " #           Pin Name   D/S              #        Pin Name      D/S ");
    terminalOut((char *) "-------------------------------------------------------------------- ");

	readAllPins();
	
    while ( count > 0 )
    {
      if ( count == 1 )
      {
          sprintf(outBfr, "%2d %20s %c %d ", staticPins[index].pinNo, staticPins[index].name,
                  getPinChar(index), readPin(staticPins[index].pinNo));
          terminalOut(outBfr);
          break;
      }
      else
      {
          sprintf(outBfr, "%2d %20s %c %d\t\t%2d %20s %c %d ", 
                  staticPins[index].pinNo, staticPins[index].name, 
                  getPinChar(index), readPin(staticPins[index].pinNo),
                  staticPins[index+1].pinNo, staticPins[index+1].name, 
                  getPinChar(index+1), readPin(staticPins[index+1].pinNo));
          terminalOut(outBfr);
          count -= 2;
          index += 2;
      }
    }

    terminalOut((char *) "D/S = Direction/State; < input, > output");
    return(0);
}

//===================================================================
//                         Status Display Screen
//===================================================================

/**
  * @name   padBuffer
  * @brief  pad outBfr with spaces 
  * @param  pos = position to start padding at in 'outBfr'
  * @retval None
  */
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

/**
  * @name   readAllPins
  * @brief  read all I/O pins into pinStates[]
  * @param  None
  * @retval None
  */
void readAllPins(void)
{
    for ( int i = 0; i < static_pin_count; i++ )
    {
        (void) readPin(staticPins[i].pinNo);
    }
}

/**
  * @name   statusCmd
  * @brief  display status screen
  * @param  argCnt = number of CLI arguments
  * @retval None
  * @note   card not required present for this to work
  */
int statusCmd(int arg)
{
    uint16_t        count = EEPROMData.status_delay_secs;
    bool            oneShot = (count == 0) ? true : false;
    float             v12I, v12V, v3p3I, v3p3V;
    
    while ( 1 )
    {
      // get voltages and currents
      get12VData(&v12I, &v12V);
      get3P3VData(&v3p3I, &v3p3V);

      readAllPins();

      CLR_SCREEN();
      CURSOR(1, 29);
      displayLine((char *) "Xavier Status Display");

      CURSOR(3,1);
      sprintf(outBfr, "TEMP WARN         %d", readPin(TEMP_WARN));
      displayLine(outBfr);

      CURSOR(3,57);
      sprintf(outBfr, "BIF [2:0]      %u%u%u", readPin(OCP_BIF2_N), readPin(OCP_BIF1_N), readPin(OCP_BIF0_N));
      displayLine(outBfr);

      CURSOR(4,1);
      sprintf(outBfr, "TEMP CRIT         %u", readPin(TEMP_CRIT));
      displayLine(outBfr);

        CURSOR(4,56);
        sprintf(outBfr, "PRSNTB [3:0]   %u%u%u%u %s", readPin(OCP_PRSNTB3_N), readPin(OCP_PRSNTB2_N), 
                readPin(OCP_PRSNTB1_N), readPin(OCP_PRSNTB0_N), isCardPresent() ? "CARD" : "VOID");
        displayLine(outBfr);

      CURSOR(5,1);
      sprintf(outBfr, "FAN ON AUX        %u", readPin(FAN_ON_AUX));
      displayLine(outBfr);

      CURSOR(5,53);
      sprintf(outBfr, "SLOT ID [1:0]       %u%u", readPin(OCP_SLOT_ID1), readPin(OCP_SLOT_ID0));
      displayLine(outBfr);

      CURSOR(6,1);
      sprintf(outBfr, "SCAN_LD_N         %d", readPin(OCP_SCAN_LD_N));
      displayLine(outBfr);

      CURSOR(6,51);
      sprintf(outBfr, "SCAN VERS [1:0]       %u%u", readPin(SCAN_VER_1), readPin(SCAN_VER_0));
      displayLine(outBfr);

      CURSOR(7,1);
      sprintf(outBfr, "AUX_PWR_EN        %d", readPin(OCP_AUX_PWR_EN));
      displayLine(outBfr);      

      CURSOR(7,56);
      sprintf(outBfr, "PCIE_PRES_N       %d", readPin(PCIE_PRES_N));
      displayLine(outBfr);

      CURSOR(8,1);
      sprintf(outBfr, "MAIN_PWR_EN       %d", readPin(OCP_MAIN_PWR_EN));
      displayLine(outBfr);  

      CURSOR(8,58);
      sprintf(outBfr, "OCP_WAKE_N      %d", readPin(OCP_WAKE_N));
      displayLine(outBfr);

      CURSOR(9,1);
      sprintf(outBfr, "RBT_ISOLATE_EN    %d", readPin(RBT_ISOLATE_EN));
      displayLine(outBfr);  

      CURSOR(9,57);
      sprintf(outBfr, "OCP_PWRBRK_N     %d", readPin(OCP_PWRBRK_N));
      displayLine(outBfr);

      CURSOR(10,1);
      sprintf(outBfr, "jmp_NIC_PWR_GOOD  %d", readPin(NIC_PWR_GOOD));
      displayLine(outBfr);  

      CURSOR(11,1);
      sprintf(outBfr, "12V: %5.2f %d  mA", v12V, (int) v12I);
      displayLine(outBfr);

      CURSOR(11,55);
      sprintf(outBfr, "3.3V: %5.2f %d mA", v3p3V, (int) v3p3I);
      displayLine(outBfr);

        if ( oneShot )
        {
            CURSOR(12,1);
            displayLine((char *) "Status delay 0, set sdelay to nonzero for this screen to loop.");
            return(0);
        }
      CURSOR(24, 22);
      displayLine((char *) "Hit any key to exit this display");

        while ( count-- > 0 )
        {
            if ( SerialUSB.available() )
            {
                // flush any user input and exit
                (void) SerialUSB.read();

                while ( SerialUSB.available() )
                {
                    (void) SerialUSB.read();
                }

                CLR_SCREEN();
                return(0);
            }

            delay(1000);
        }

        count = EEPROMData.status_delay_secs;
    }

    return(0);

} // statusCmd()


/**
  * @name   getPinName
  * @brief  get name of pin
  * @param  Arduino pin number
  * @retval pointer to name or 'unknown' if not found
  */
const char *getPinName(int pinNo)
{
    for ( int i = 0; i < static_pin_count; i++ )
    {
        if ( pinNo == staticPins[i].pinNo )
            return(staticPins[i].name);
    }

    return("Unknown");
}

/**
  * @name   getPinIndex
  * @brief  get index into static/dynamic pin arrays
  * @param  Arduino pin number
  * @retval None
  */
int8_t getPinIndex(uint8_t pinNo)
{
    for ( int i = 0; i < static_pin_count; i++ )
    {
        if ( staticPins[i].pinNo == pinNo )
            return(i);
    }

    return(-1);
}

/**
  * @name   set_help
  * @brief  help for set command
  * @param  None
  * @retval None
  */
void set_help(void)
{
    terminalOut((char *) "FLASH Parameters are:");
    sprintf(outBfr, "  sdelay <integer> - status display delay in seconds; current: %d", EEPROMData.status_delay_secs);
    terminalOut(outBfr);
    sprintf(outBfr, "  pdelay <integer> - power up sequence delay in milliseconds; current: %d", EEPROMData.pwr_seq_delay_msec);
    terminalOut(outBfr);
    terminalOut((char *) "'set <parameter> <value>' sets a parameter from list above to value");
    terminalOut((char *) "  value can be <integer>, <string> or <float> depending on the parameter");

    // TODO add more set command help here
}

/**
  * @name   setCmd
  * @brief  Set a parameter (seeing) in FLASH
  * @param  arg 1 = parameter name
  * @param  arg 2 = value to set
  * @retval 0
  * @note   no args shows help w/current values
  * @note   simulated EEPROM is called FLASH to the user
  */
int setCmd(int argCnt)
{

    char          *parameter = tokens[1];
    String        valueEntered = tokens[2];
//    float         fValue;
    int           iValue;
    bool          isDirty = false;

    if ( argCnt == 0 )
    {
        set_help();
        return(0);
    }
    else if ( argCnt != 2 )
    {
        set_help();
        return(0);
    }

    if ( strcmp(parameter, "sdelay") == 0 )
    {
        iValue = valueEntered.toInt();
        if (EEPROMData.status_delay_secs != iValue )
        {
          isDirty = true;
          EEPROMData.status_delay_secs = iValue;
        }
    }
    else if ( strcmp(parameter, "pdelay") == 0 )
    {
        iValue = valueEntered.toInt();
        if (EEPROMData.pwr_seq_delay_msec != iValue )
        {
          isDirty = true;
          EEPROMData.pwr_seq_delay_msec = iValue;
        }
    }
    else
    {
        terminalOut((char *) "Invalid parameter name");
        set_help();
        return(1);
    }

    if ( isDirty == true )
        EEPROM_Save();

    return(0);

} // setCmd()

/**
  * @name   queryScanChain
  * @brief  extract info from scan chain output
  * @param  displayResults  true to display results, else false
  * @retval uint32_t    32 bits of scan chain data received
  */
uint32_t queryScanChain(bool displayResults)
{
    uint8_t             shift = 31;
    char                *s = outBfr;
    const char          fmt[] = "%-20s ... %d    ";
    unsigned            i = 0;

    timers_scanChainCapture();

    if ( displayResults == false )
        return(scanShiftRegister_0);

    sprintf(outBfr, "scan chain shift register 0: %08X", (unsigned int) scanShiftRegister_0);
    terminalOut(outBfr);

    // WARNING: This code below expects the entries in scanBitNames[] to be in order order 31..0
    // to align with incoming shifted left bits from SCAN_DATA_IN
    while ( i < 32 )
    {
        sprintf(s, fmt, scanBitNames[i++].bitName, (scanShiftRegister_0 & (1 << shift--)) ? 1 : 0);
        s += 30;
        sprintf(s, fmt, scanBitNames[i++].bitName, (scanShiftRegister_0 & (1 << shift--)) ? 1 : 0);
        s += 30;
        *s = 0;

        terminalOut(outBfr);
        s = outBfr;
    }

    return(scanShiftRegister_0);
    
} // queryScanChain()

/**
  * @name   pwrCmdHelp
  * @brief  display help for the power command
  * @param  None
  * @retval None
  */
static void pwrCmdHelp(void)
{
    terminalOut((char *) "Usage: power <up | down | status> <main | aux | card>");
    terminalOut((char *) "  'power status' requires no argument and shows the power status of NIC card");
    terminalOut((char *) "  main = MAIN_EN to NIC card; aux = AUX_EN to NIC card; ");
    terminalOut((char *) "  card = MAIN_EN=1 then pdelay msecs then AUX_EN=1; see 'set' command for pdelay");
}

/**
  * @name   pwrCmd
  * @brief  Control AUX and MAIN power to NIC 3.0 board
  * @param  argCnt  number of arguments
  * @param  tokens[1]  up, down or status
  * @param  tokens[2]   main, aux or card
  * @retval 0   OK
  * @retval 1   error
  * @note   Delay is changed with 'set pdelay <msec>'
  * @note   power <up|down|status> <main|aux|board> status has no 2nd arg
  */
int pwrCmd(int argCnt)
{
    int             rc = 0;
    bool            isPowered = false;
    uint8_t         mainPin = readPin(OCP_MAIN_PWR_EN);
    uint8_t         auxPin = readPin(OCP_AUX_PWR_EN);

    if ( argCnt == 0 )
    {
        pwrCmdHelp();
        return(1);
    }

    if ( isCardPresent() == false )
    {
        terminalOut((char *) "NIC card is not present; no power info available");
        return(1);
    }

    if (  mainPin == 1 &&  auxPin == 1 )
        isPowered = true;

    if ( argCnt == 1 )
    {
        if ( strcmp(tokens[1], "status") == 0 )
        {
            sprintf(outBfr, "Status: NIC card is powered %s", (isPowered) ? "up" : "down");
            SHOW();
            return(rc);
        }
        else
        {
            terminalOut((char *) "Incorrect number of command arguments");
            pwrCmdHelp();
            return(1);
        }
    }
    else if ( argCnt != 2 )
    {
        terminalOut((char *) "Incorrect number of command arguments");
        pwrCmdHelp();
        return(1);
    }

    if ( strcmp(tokens[1], "up") == 0 )
    {
        if ( strcmp(tokens[2], "card") == 0 )
        {
            if ( isPowered == false )
            {
                sprintf(outBfr, "Starting NIC power up sequence, delay = %d msec", EEPROMData.pwr_seq_delay_msec);
                SHOW();
                writePin(OCP_MAIN_PWR_EN, 1);
                delay(EEPROMData.pwr_seq_delay_msec);
                writePin(OCP_AUX_PWR_EN, 1);
                terminalOut((char *) "Waiting for scan chain data...");
                delay(2000);
                queryScanChain(false);
                queryScanChain(true);
                terminalOut((char *) "Power up sequence complete");
            }
            else
            {
                terminalOut((char *) "Power is already up on NIC card");
            }
        }
        else if ( strcmp(tokens[2], "main") == 0 )
        {
            if ( mainPin == 1 )
            {
                terminalOut((char *) "MAIN_EN is already 1");
                return(0);
            }
            else
            {
                writePin(OCP_MAIN_PWR_EN, 1);
                terminalOut((char *) "Set MAIN_EN to 1");
                return(0);
            }
        }
        else if ( strcmp(tokens[2], "aux") == 0 )
        {
            if ( auxPin == 1 )
            {
                terminalOut((char *) "AUX_EN is already 1");
                return(0);
            }
            else
            {
                writePin(OCP_AUX_PWR_EN, 1);
                terminalOut((char *) "Set AUX_EN to 1");
                return(0);
            }
        }
        else
        {
            terminalOut((char *) "Invalid argument");
            pwrCmdHelp();
            return(1);
        }
    }
    else if ( strcmp(tokens[1], "down") == 0 )
    {
        if ( strcmp(tokens[2], "card") == 0 )
        {
            if ( isPowered == true )
            {
                writePin(OCP_MAIN_PWR_EN, 0);
                writePin(OCP_AUX_PWR_EN, 0);
                terminalOut((char *) "Powered down NIC card");
            }
            else
            {
                terminalOut((char *) "Power is already down on NIC card");
            }
        }
        else if ( strcmp(tokens[2], "main") == 0 )
        {
            if ( mainPin == 0 )
            {
                terminalOut((char *) "MAIN_PWR_EN is already 0");
                return(0);
            }
            else
            {
                writePin(OCP_MAIN_PWR_EN, 0);
                terminalOut((char *) "Set MAIN_PWR_EN to 0");
                return(0);
            }
        }
        else if ( strcmp(tokens[2], "aux") == 0 )
        {
            if ( auxPin == 0 )
            {
                terminalOut((char *) "AUX_PWR_EN is already 0");
                return(0);
            }
            else
            {
                writePin(OCP_AUX_PWR_EN, 0);
                terminalOut((char *) "Set AUX_PWR_EN to 0");
                return(0);
            }
        }
        else
        {
            terminalOut((char *) "Invalid argument");
            pwrCmdHelp();
            return(1);
        }
    }
    else
    {
        terminalOut((char *) "Invalid subcommand: use 'up', 'down' or 'status'");
        rc = 1;
    }

    return(rc);
}

/**
  * @name   versCmd
  * @brief  Displays firmware version
  * @param  arg not used
  * @retval None
  */
int versCmd(int arg)
{
    sprintf(outBfr, "Firmware version %s built on %s at %s", VERSION_ID, BUILD_DATE, BUILD_TIME);
    terminalOut(outBfr);
    return(0);
}

/**
  * @name   isCardPresent
  * @brief  Determine if NIC card is present
  * @param  None
  * @retval true if card present, else false
  */
bool isCardPresent(void)
{
    uint8_t         present = readPin(OCP_PRSNTB0_N);

    present |= (readPin(OCP_PRSNTB1_N) << 1);
    present |= (readPin(OCP_PRSNTB2_N) << 2);
    present |= (readPin(OCP_PRSNTB3_N) << 3);

    if ( present == 0xF )
        return(false);
    
    return(true);
}


/**
  * @name   scanCmd
  * @brief  implement scan command
  * @param  argCnt  not used
  * @retval int 0=OK, 1=error
  */
int scanCmd(int argCnt)
{
    if ( isCardPresent() )
    {
        queryScanChain(true);
    }
    else
    {
        terminalOut((char *) "NIC card is not present; cannot query scan chain");
    }

    return(0);
}