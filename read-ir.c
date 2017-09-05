#include "read-ir.h"
#include <memory.h>

#if (defined(__AVR__))
#include <avr\pgmspace.h>

#elif (defined(__PUREC__))
#define PROGMEM
#include <stdio.h>

#else
#include <pgmspace.h>

#endif

#define MARGIN 0.2
#define MARGINL(x) (uint16_t)(x*(1-MARGIN))
#define MARGINR(x) (uint16_t)(x*(1+MARGIN))

#define TNEC 562
#define TKADEN 425
#define TSONY 600

#define ARRAYSIZE_U8(x) ((uint8_t)(sizeof(x)/sizeof(x[0])))

typedef struct RemoFormat_t {
  uint16_t leader[2][2];
  uint16_t data0[2];
  uint16_t data1[2];
  uint16_t repeater[2];
} RemoFormat;

const RemoFormat remoFormat[] PROGMEM = {
  // NEC
  {
    {{MARGINL(16*TNEC), MARGINR(16*TNEC)}, {MARGINL(8*TNEC), MARGINR(8*TNEC)}},
    {MARGINL(1*TNEC), MARGINR(1*TNEC)},
    {MARGINL(3*TNEC), MARGINR(3*TNEC)},
    {MARGINL(4*TNEC), MARGINR(4*TNEC)},
  },
  // KADENKYO
  {
    {{MARGINL(8*TKADEN), MARGINR(8*TKADEN)}, {MARGINL(4*TKADEN), MARGINR(4*TKADEN)}},
    {MARGINL(1*TKADEN), MARGINR(1*TKADEN)},
    {MARGINL(3*TKADEN), MARGINR(3*TKADEN)},
    {MARGINL(8*TKADEN), MARGINR(8*TKADEN)},
  },
  // SONY
  {
    {{MARGINL(4*TKADEN), MARGINR(4*TKADEN)}, {0, 0}},
    {MARGINL(1*TKADEN), MARGINR(1*TKADEN)},
    {MARGINL(2*TKADEN), MARGINR(2*TKADEN)},
    {0, 0},
  },
};


typedef struct RemoData_t {
  uint8_t type; // 0:data 1:same as first 2:repeat code 3:separator
  union {
    struct {
      uint8_t len;
      uint8_t data[1]; // size are greater than or equal to 1
    } data;
    unsigned long separator;
  };
} RemoData;

typedef struct Remo_t {
  uint16_t averateT; // [us]
  uint16_t averateSeperator;  // [10us]
  uint8_t format; // 0:unknown 1:nec 2:kadenkyo 3:sony
  uint8_t dataNum;
  uint8_t dataOffset[10];
} Remo;


RemoData *_remoDataPtr(int8_t index);
void _incrementRemoData();
int8_t _parseLeader(uint32_t time);
int8_t _parseData(uint32_t time, uint8_t signal);

Remo *remo;
RemoData *remoData;

uint8_t readByte;
uint8_t readPos = 0;
uint16_t leader[2];

void initRemo(void *buffer, uint8_t size) {
  memset(buffer, 0, size);
  remo = (Remo*)buffer;
  remoData = (RemoData*)(buffer + sizeof(Remo));
  remo->dataNum = 1;
  leader[0] = leader[1] = 0;
}


// @return less than 0:error 0:keep reading 1:end
int8_t parseRemo(uint32_t time, uint8_t signal) {
  if (remo->format == FMT_UNKNOWN) {
    if (_parseLeader(time) < 0)
      return -1;
  } else {
    int8_t ret = _parseData(time, signal);
    if (ret < 0)
      return -1;
    else if (ret == 1)
      return 1;
  }
  return 0;
}

void outRemo() {
}




RemoData *_remoDataPtr(int8_t index) {
  //uint8_t i;
  uint8_t *ptr = (uint8_t*)remoData;
  if (index == -1) {
    index = (int8_t)remo->dataNum - 1;
  }
  return (RemoData*)(ptr + remo->dataOffset[index]);
}

void _incrementRemoData() {
  uint8_t size = sizeof(RemoData);
  RemoData *cur = _remoDataPtr(-1);
  if (cur->type == 0) {
    size += cur->data.len;
  }
  remo->dataNum++;
  remo->dataOffset[remo->dataNum-1] = remo->dataOffset[remo->dataNum-2] + size;
}

int8_t _parseLeader(uint32_t time) {
  if (time > 32767)
    return -1;
  if (leader[0] == 0) {
    leader[0] = (uint16_t)time;
    return 0;
  }
  leader[1] = (uint16_t)time;
  for (uint8_t i=0; i<ARRAYSIZE_U8(remoFormat); i++) {
    const RemoFormat *prf = &remoFormat[i];
    if (leader[0] < prf->leader[0][0] || prf->leader[0][1] < leader[0])
      continue;
    if (prf->leader[1][0] != 0) {
      if (leader[1] < prf->leader[1][0] || prf->leader[1][1] < leader[1])
        continue;
    }
    remo->format = i + 1; // FMT_NEC, FMT_NEC or FMT_SONY
    printf("format => %d\n", remo->format);
    return 1;
  }
  return -2;
}

int8_t _parseData(uint32_t time, uint8_t signal) {
  const RemoFormat *prf = &remoFormat[remo->format - 1];
  const uint8_t timingEdge = (prf->leader[1][0] == 0) ? 0 : 1;
  if (signal == timingEdge) {
    if (time < prf->data0[0])
      return -1;
    if (time <= prf->data0[1])
      return 0;
    if (timingEdge == 0) {
      // NEC or KADENKYO
      if (prf->leader[0][0] <= time && time <= prf->leader[0][1])
        ; // TODO:データが調度終わっているかcheckして終わっているなら、新しいデータ or リピーターとして繰越
    } else {
      // SONY
      if (prf->leader[0][1] < time) {
        ; // TODO:データが調度終わっているかcheckして終わっているなら、セパレーターとして繰越
      }
    }
    return -1;
  } else {
    if (prf->data1[0] <= time && time <= prf->leader[0][1])
      ;
  }
  return 0;
}
