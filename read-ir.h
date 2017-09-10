#ifndef _READ_IR_H_
#define _READ_IR_H_

#include <stdint.h>

#define FMT_UNKNOWN 0
#define FMT_NEC 1
#define FMT_KADENKYO 2
#define FMT_SONY 3

typedef struct RemoFrame_t {
  // unit is 1us * 8 = 8us.
  // if time is 200ms then 'time' value are 25000 (== 200*1000/8).
  uint16_t time;
  // TYPE_xxx
  uint8_t type;
  // data length of bits
  uint8_t dataBits;
  // size are greater than or equal to 1
  uint8_t data[0];
} RemoFrame;

typedef struct Remo_t {
  uint8_t format; // 0:unknown 1:nec 2:kadenkyo 3:sony
  uint8_t frameNum;
  uint8_t frameOffset[10];
  uint32_t frameTime;
  uint32_t edgeTime; // minimun period time of data edge
} Remo;

typedef struct RemoWork_t {
  uint16_t numT;
  uint16_t leader[2];
  uint8_t readState; // 0:unknown 1:parsing frame
  uint8_t frameBuffSize;
} RemoWork;

void initRemo(void *buffer, uint8_t buffSize, void *workBuff);
int8_t parseRemo(uint32_t time, uint8_t signal);
void outRemo();

RemoFrame *remoFramePtr(int8_t index);
int8_t incrementRemoFrame(void);

#endif
