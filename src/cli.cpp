#include <Arduino.h>
#include "main.h"
#include "cli.h"
#include "commands.h"

// Constant Data

const char      cliPrompt[] = "cmd> ";
const int       promptLen = sizeof(cliPrompt);

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
    {"eeprom", eepromCmd,  -1, "Displays FRU EEPROM info areas if no args.",     "'eeprom <addr> <length>' dumps <length> bytes @ <addr>"},
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
