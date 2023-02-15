#ifndef _COMMANDS_H_
#define _COMMANDS_H_

void monitorsInit(void);

const char *getPinName(int pinNo);
int8_t getPinIndex(uint8_t pinNo);
int statusCmd(int arg);
char *padBuffer(int pos);


#endif // _COMMANDS_H_