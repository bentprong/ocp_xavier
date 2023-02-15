#ifndef _MAIN_H_
#define _MAIN_H_

#include <Arduino.h>
#include <stdint-gcc.h>
#include "commands.h"
#include "cli.h"
#include "debug.h"
#include "eeprom.h"
#include "float.h"
#include "time.h"

#define VERSION               "1.0.2"

// alises to support pin_mgt_t pinFunc - because Arduino doesn't
// "like" writing to an input pin
#define INPUT_PIN             INPUT
#define OUTPUT_PIN            OUTPUT
#define IN_OUT_PIN            4             // reserved future when DIP switches won't short

#define OUTBFR_SIZE                 (MAX_LINE_SZ * 3)

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

// ANSI terminal escape sequence
#define CLR_SCREEN()                terminalOut((char *) "\x1b[2J")
#define CLR_LINE()                  terminalOut((char *) "\x1b[0K")
#define SHOW()                      terminalOut(outBfr)

// Pin Management structure
typedef struct {
  uint8_t           pinNo;
  uint8_t           pinFunc;
  char              name[20];
} pin_mgt_t;

// misc functions
void dumpMem(unsigned char *s, int len);


const char *getPinName(int pinNo);
int8_t getPinIndex(uint8_t pinNo);


#endif // _MAIN_H_