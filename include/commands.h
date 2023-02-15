#ifndef _COMMANDS_H_
#define _COMMANDS_H_

void monitorsInit(void);
int pinCmd(int arg);
const char *getPinName(int pinNo);
int8_t getPinIndex(uint8_t pinNo);
int statusCmd(int arg);
char *padBuffer(int pos);
int curCmd(int arg);
int writeCmd(int arg);
int readCmd(int arg);
int setCmd(int arg);

#endif // _COMMANDS_H_