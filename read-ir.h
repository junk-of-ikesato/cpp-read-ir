#ifndef _READ_IR_H_
#define _READ_IR_H_

#include <stdint.h>

#define FMT_UNKNOWN 0
#define FMT_NEC 1
#define FMT_KADENKYO 2
#define FMT_SONY 3


void initRemo(void *buffer, uint8_t size);
int8_t parseRemo(uint32_t time, uint8_t signal);
void outRemo();

#endif
