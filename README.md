# ocp_xavier
OCP Xavier NIC 3.0 Test Board
Written by Richard Lewis (rick@fmspcb.com) for Fusion Manufacturing Services.

Posted February 18, 2023 at https://github.com/bentprong/ocp_xavier
Initial Firmware Release: v1.0.4

## Release v1.1.0 May 8, 2023
1. Fixed issue with Windows/TeraTerm Issue #10 as best as could be done. See note in
section below on Terminal Instructions which have been updated as well.
2. Fixed uncaught potential serial buffer overflow Issue #12

## Release v1.0.6 April 23, 2023
Merged new features from TTF project here including scan chain, card detection, and new 
commands/help.

## Release v1.0.5 April 19, 2023
Updated Arduino files in the 'platformio' subdirectory of this repo.

## Overview
These instructions have been tested on Windows and Mac.  If you are using a variant of Linux, the
instructions for Mac should apply except possibly for the "screen" terminal emulator program.  At
a minimum you need to select a Linux terminal emulation program and open the USB TTY with it.

This project was initially cloned from the OCP Vulcan project then refactored to split the source
code into multiple files to ease readability and management.  Primary reason for cloning that
project was to re-use the USB serial code that was working, plus the simulated EEPROM and CLI.

The project is built using the PlatformIO extension for Visual Studio Code.  An ATMEL-ICE was
used to debug the code on the board and can be used to program the binary using Microchip Studio.

## Operating Instructions
Enter the 'help' command to get a list of the available commands, and details about usage of
each command.

The simulated EEPROM (in FLASH) is only used to set 'sdelay' the delay between status page
updates.   You can verify that it is operational by entering 'debug eeprom' command. The
'set' command can be used to change this delay 'set sdelay <secs>' where <secs> is an integer
value in seconds.

Do  not confuse this simulated EEPROM with the FRU EEPROM on a NIC 3.0 board.  The command to
access FRU EEPROM contents is just 'eepom' (see help for more).

The signature of the simulated EEPROM should always be DE110C02.  

--------------------------------------------------------------------------------------------------
WARNING: Loading the board with (new) firmware WILL erase the EEPROM and you will need to re-enter
the settings afterwards.  The board may display "EEPROM Validation Failed..." to indicate that
this has happened, but it is a good practice to always enter the settings after flashing the
board's firmware.  There are no settings defined as of the initial release of the firmware.
--------------------------------------------------------------------------------------------------

### Tips:
Backspace and delete are implemented and erase the previous character typed.
Up arrow executes the previous command.
In this document, <ENTER> means press the keyboard Enter key.

## Getting Started
Follow the Wiring and Terminal Instructions below to get started using the Xavier board.

The board firmware prompt is "cmd>" and when you see that, you can enter "help<ENTER>" for help on the
available commands.

The purpose of the board is to provide debug capabilities for NIC 3.0 cards.  

## Development Environment Setup
This varies slightly between platforms including Mac, Linux and Windows.

1. Download and install Visual Studio Code.

2. In VS Code ("VSC"), go to Extensions and seach for "platformio" and install it.  This will take some time,
watch the status area in the bottom right of VSC for progress.  Note that PlatformIO ("PIO") also installs the C/C++ 
extension which is needed.

When finished you will see a prompt "Please restart VSCode" so do that.

Windows only: It is recommended that you install cygwin so that you have a bash terminal to use git.  There are other 
options such as GitBash.  Point is, command-line git needs to be run to clone the source code repo unless you 
already have/know a tool that will allow you to clone and manage a GitHub repository.

3. Set up a Projects folder if you don't already have one.  For these instructions it is assumed that this is
<home>/Documents/Projects.  VSC "may" be able to accomodate other directory structures, but of course, those
cannot and have not been tested.

    Windows:  <home> = C:/Users/<username>
    Mac:      <home> = Users/<username>

4. Log into GitHub.com using your own credentials then clone this repository: 
    bentprong/ocp_xavier
    
into your Projects folder.

    GitHub Requirements:
        a. SSH key generated and installed on this computer for YOU
        b. SSH key for YOU installed in YOUR GitHub.com account

5. In VSC, choose File | Open Folder... and navigate to <home>/<Projects>/ocp_xavier then highlight that, and
click Select Folder.

6. In VSC, click the checkmark in the blue bar at the bottom to build.  This should install necessary files 
and tools.  It may take quite a bit of time.

7. In the repo folder platformio, open the README file and follow the instructions to configure PIO for the
Xavier board.  There are 2 steps to this process explained in the README.

--------------------------------------------------------------------------------------------------
** FLASHING NOTE ** Failure to exactly follow the instructions in the README in step #7 will
result in the code not building correctly!   That is because the files that must be copied or
updated define the pinouts for the Xavier project.  You have been warned!
--------------------------------------------------------------------------------------------------

## Wiring Instructions
1. Connect Xavier board to ATMEL-ICE and connect ATMEL-ICE to computer (debug only)
2. Connect Xavier board USB-C port to computer USB port using a DATA CABLE (not a charging only cable).
3. Windows only: If not already installed, install a terminal emulator program such as TeraTerm.
 
LED BEHAVIOR: Once the Xavier board has been powered up, there will be 2 LEDs that are on solid: one
for 3.3V and one for 12V.  These 2 LEDs are near the Power connector.  If either is off, a hardware issue
exists in one or both power supplies.  These 2 LEDs should always be on.

A third LED may be lit if a NIC 3.0 board is inserted into the bay and the power is good to that board. 
This is the OCP_PWR_GOOD LED.

A fourth MCU_LED near the P-UART1 connector should be fast blinking (4-5 times a second).  This 
means that the board has initialized OK and is waiting on a USB/serial connection.  For more O/S-
specific information on LED behavior, please see Terminal Instructions below.

If the MCU_LED is on solid, an initialization error occured.  Best bet is to program the board again.
If all LEDs are off, then the board is not receiving any power.  Check the USB
cable and that it is firmly in the USB connector.

NOTE: The board is supposed to be able to be powered from USB 5V, if the SW_DEV_PWR jumper is
installed.  However, that does not to work.  You must have an ATX power supply connected to the
appropriate connector on Xavier.  The setting of the SW_DEV_PWR jumper doesn't matter.

Once a connection is made, the MCU_LED will slow blink to indicate a heartbeat from the board and
firmware.

If the MCU_LED is off, the board firmware never started up.   The firmware turns this LED on when it
first starts, then it initializes itself and the harware, then it starts fast blinking unless errors
were encountered in which case it stays on.

## Build/Debug Instructions
In VSC, click Run | Start Debugging.  The code will be built for debug and you should see the debugger
stop in main() at the init() call.   Click the blue |> icon in the debugger control area of VSC.

NOTE: Sometimes when using the debugger, the serial over USB does not immediately connect.  See 
Terminal Instructions below for more info.

## Firmware Upload
To program release firmware in VSC, click the -> in the blue bottom line of VSC.  Requires ATMEL-ICE.

## Binary Executable Instructions
Firmware is prebuilt in GitHub and located at:
    bentprong/ocp_xavier/.pio/build/samd21g18a/firmware.bin

    https://github.com/bentprong/ocp_xavier/tree/main/.pio/build/samd21g18a

Use any flash utility such as Microchip Studio to erase and flash this .bin file into the Xavier board.

NOTE: PIO "upload" does not work because there is intentionally no bootloader on the Xavier board.  At this
time, this OCP project does not support Arduino sketches.

## Building Release Firmware
It is not necessary to build the firmware in order to use the released firmware.  In the paragraph
below is the location of the firmware.bin file that needs to be programmed into the board using a tool
such as Microchip Studio.

To build release firmware, in VSC, click the checkmark in the blue line at the bottom.  If no problems
are reported (there should be none), the executable is located in the local directory here:
    <home>/Projects/ocp-xavier/.pio/build/samd21g18a/firmware.bin

Note that in this same location is also the firmware.elf file which is the debug version of firmware.

Use any flash utility such as Microchip Studio to erase the chip and burn this .bin file into the Xavier 
board.

NOTE: PIO "upload" does not work, because there is intentionally no bootloader on the Xavier board. This
firmware is NOT an Arduino sketch.

## Microchip Studio
1. Open Studio then select Tools | Device Programming.
2. Select Atmel-ICE from the Tool pulldown menu.  Verify that the Device is ATSAMD21G18A then click
the Apply button.  A menu of options will appear on the left of the screen now.
3. Select Memories then Erase Chip & Erase now.
4. In the Flash (256KB) section, use the ... button to navigate to the binary file described above.
That full path and filename should be shown in the long text area.
5. Click the Program button and wait for the status messages which should be:
    Erasing device...OK
    Programming Flash...OK
    Verifying Flash...OK
6. Click the Close button.

---
NOTE:  There may be a conflict between VSC and Microchip Studio if you are trying to use Studio to
flash the firmware.  Close out VSC and Studio, then restart Studio.  The conflict would be in these
2 software programs trying to use the Atmel-ICE at the same time.  If the problem persists, close
Studio, unplug the Atmel-ICE, wait a few seconds, plug the Atmel-ICE back in, then restart Studio.
---

## Terminal Instructions
Serial over USB does not require any of the traditional UART parameters such as baud rate. However,
many terminal programs do require this, so use 115200 in all tools.

### Windows
It is helpful to have Device Manager ("DM") open and the Ports (COM & LPT) section expanded.  When first
plugging the Xavier board into a Windows computer and powering up Xavier, a new COM port will be enumerated 
for the serial terminal on Xavier.   There is often an Intel(R) Active Management Technology - SOL (COMn) 
port already showing in DM that is NOT the COM port for Xavier.

Once you see the COM port in DM, open TeraTerm, then start a new serial connection on the new COM port. 
You should see the Xavier welcome message and Xavier prompt cmd> in the TeraTerm window.  

If the board is powered down, you must close TeraTerm. After the board is powered back up, 
and the COM port is shown in DM, re-open TeraTerm and start a new connection.  Note that there is a LED
on Xavier that remains on even when ATX power is shut down.  This may be back-powering some circuitry
on the board. A data-only USB cable is recommended as a possible solution.

The reset button on Xavier will not reliably re-establish a serial connection.

It is important that TeraTerm be shut down and a brief delay occur before turning the board back
on. Also, after loading firmware with the debugger, a power cycle is often required.

Once the connection is established, the MCU LED will start slow blinking.

There is no fast-blink feature of the LED starting with v1.1.0. 

### Mac: 
Get a listing of TTYs like this:
    user@computer ~ % ls -l /dev/tty.usb*
    crw-rw-rw-  1 root  wheel    9,   2 Jan 17 14:02 /dev/tty.usbmodem146201 

Enter "screen /dev/tty.usbmodem146201 115200" (or whatever the output of the ls command indicates)
and you should see the Xavier welcome message and prompt cmd> in the terminal window.  While the
baud rate of 115200 doesn't apply to serial over USB, it is required by the screen command.

### Linux (eg Ubuntu)
You can install 'screen' or 'minicom' using apt.  For screen, use the ls command
as shown in the Mac section above to find the USBn device, then enter the command:
"screen /dev/ttyUSB0 115200" for example if the connection is on ttyUSB0.  

For minicom, please search online for a tutorial on installation and usage.

For Mac and Linux, if Xavier is powered down, you will see the connection drop in screen.  After the
board is powered back up, use up arrow or enter the same command used to start the connection.

## Issues
See:
    https://github.com/bentprong/ocp_xavier/issues

