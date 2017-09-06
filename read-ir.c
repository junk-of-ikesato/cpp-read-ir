#include "read-ir.h"
#include <memory.h>

#if (defined(__AVR__))
#include <avr\pgmspace.h>
#define assert(x)

#elif (defined(__PUREC__))
#define PROGMEM
#include <stdio.h>
#include <assert.h>

#else
#include <pgmspace.h>
#define assert(x)

#endif

#define MARGIN 0.2
#define MARGINL(x) (uint16_t)(x*(1-MARGIN))
#define MARGINR(x) (uint16_t)(x*(1+MARGIN))

#define TNEC 562
#define TKADEN 425
#define TSONY 600

#define TYPE_UNKNOWN 0
#define TYPE_DATA 1
#define TYPE_SAME_AS_FIRST 2
#define TYPE_REPEATER 3

#define ARRAYSIZE_U8(x) ((uint8_t)(sizeof(x)/sizeof(x[0])))

typedef struct RemoFormat_t {
  uint16_t leader[2][2];
  uint16_t data0[2];
  uint16_t data1[2];
  uint16_t repeater[2];
  struct {
    unsigned leader : 5;
    unsigned data0 : 3;
    unsigned data1 : 3;
    unsigned repeater : 5;
  } t;
} RemoFormat;

const RemoFormat remoFormat[] PROGMEM = {
  // NEC
  {
    {{MARGINL(16*TNEC), MARGINR(16*TNEC)}, {MARGINL(8*TNEC), MARGINR(8*TNEC)}},
    {MARGINL(1*TNEC), MARGINR(1*TNEC)},
    {MARGINL(3*TNEC), MARGINR(3*TNEC)},
    {MARGINL(4*TNEC), MARGINR(4*TNEC)},
    {16+8, 1, 3, 16+4},
  },
  // KADENKYO
  {
    {{MARGINL(8*TKADEN), MARGINR(8*TKADEN)}, {MARGINL(4*TKADEN), MARGINR(4*TKADEN)}},
    {MARGINL(1*TKADEN), MARGINR(1*TKADEN)},
    {MARGINL(3*TKADEN), MARGINR(3*TKADEN)},
    {MARGINL(8*TKADEN), MARGINR(8*TKADEN)},
    {8+4, 1, 3, 8+8},
  },
  // SONY
  {
    {{MARGINL(4*TKADEN), MARGINR(4*TKADEN)}, {0, 0}},
    {MARGINL(1*TKADEN), MARGINR(1*TKADEN)},
    {MARGINL(2*TKADEN), MARGINR(2*TKADEN)},
    {0, 0},
    {4+0, 1, 2, 0},
  },
};


typedef struct RemoFrame_t {
  // unit is 1us * 8 = 8us.
  // if time is 200ms then 'time' value are 25000 (== 200*1000/8).
  uint16_t time;
  // TYPE_xxx
  uint8_t type : 2;
  // data length
  uint8_t dataLen : 6;
  // size are greater than or equal to 1
  uint8_t data[1];
} RemoFrame;

typedef struct Remo_t {
  uint8_t format; // 0:unknown 1:nec 2:kadenkyo 3:sony
  uint8_t frameNum;
  uint8_t frameOffset[10];

  // work variables
  uint8_t readByte;
  uint8_t readPos;
  uint8_t readState; // 0:unknown 1:parsing frame
  uint16_t leader[2];
  uint16_t numT;
  uint16_t sumT;
} Remo;


RemoFrame *_remoFramePtr(int8_t index);
void _incrementRemoFrame(void);
int8_t _parseLeader(uint32_t time, uint8_t signal);
int8_t _parseData(uint32_t time, uint8_t signal);
void _storeData(uint8_t v, uint32_t time);
void _storeFrameTime(uint32_t time);
void _applySameData(void);

Remo *remo;
RemoFrame *remoFrame;

void initRemo(void *buffer, uint8_t size) {
  memset(buffer, 0, size);
  remo = (Remo*)buffer;
  remoFrame = (RemoFrame*)(buffer + sizeof(Remo));
}


// @return <0:error 0:keep reading 1:end
int8_t parseRemo(uint32_t time, uint8_t signal) {
  int8_t ret;
  if (remo->readState == 0) {
    ret = _parseLeader(time, signal);
    if (ret < 0)
      return -1;
    if (ret == 1) {
      remo->readState = 1;
      RemoFrame *cur = _remoFramePtr(-1);
      const RemoFormat *prf = &remoFormat[remo->format - 1];
      uint16_t ltime = (uint16_t)(remo->leader[0] + remo->leader[1]) >> 3;
      cur->time = ltime;
      remo->sumT = (uint16_t)(remo->sumT + time);
      if (cur->type == TYPE_DATA)
        remo->numT = (uint16_t)(remo->numT + prf->t.leader);
      else // == TYPE_REPEATER
        remo->numT = (uint16_t)(remo->numT + prf->t.repeater);
    }
  } else if (remo->readState == 1) {
    int8_t ret = _parseData(time, signal);
    if (ret < 0)
      return -2;
    else if (ret == 1)
      return 1;
    else if (ret == 2) {
      _applySameData();
      remo->readState = 0;
    }
  }
  return 0;
}

void outRemo() {
  static const char *formatStr[] = {"UNKNOWN", "NEC", "KADENKYO", "SONY"};
  static const char *typeStr[] = {"UNKNOWN", "DATA", "SAME", "REP"};
  RemoFrame *p;
  uint32_t averageT = (uint32_t)(remo->sumT / remo->numT);
  uint32_t averageFrame=0;
  uint16_t i;
  for (i=0; i<remo->frameNum-1; i++) {
    p = _remoFramePtr((int8_t)i);
    averageFrame += p->time;
  }
  averageFrame = averageFrame / i;
  printf("avarage     : %d, %d (T, frame) [us]\n", averageT << 3, averageFrame << 3);
  printf("format      : %s\n", formatStr[remo->format]);
  printf("frameNum    : %d\n", remo->frameNum);
  printf("frameOffset :");
  for (int i=0; i<(int)sizeof(remo->frameOffset); i++) {
    printf(" %02x", remo->frameOffset[i]);
  }
  printf("\n");
  printf("work        : %02x %d %d {%04x, %04x}\n",
         remo->readByte, remo->readPos, remo->readState,
         remo->leader[0], remo->leader[1]);
  printf("frames      : [");
  for (int8_t i=0; i<remo->frameNum; i++) {
    RemoFrame *p = _remoFramePtr(i);
    uint32_t time = (uint32_t)(p->time << 3);
    if (i>0)
      printf(", ");
    printf("%s", typeStr[p->type]);
    if (p->type == TYPE_DATA) {
      printf("[");
      for (uint8_t j=0; j<p->dataLen; j++) {
        if (j>0)
          printf(" ");
        printf("%02x", p->data[j]);
      }
      printf("]");
    }
    printf("(%d[us])", time);
  }
  printf("]\n");
}




RemoFrame *_remoFramePtr(int8_t index) {
  //uint8_t i;
  uint8_t *ptr = (uint8_t*)remoFrame;
  if (index == -1) {
    index = (int8_t)(remo->frameNum - 1);
  }
  return (RemoFrame*)(ptr + remo->frameOffset[index]);
}

void _incrementRemoFrame(void) {
  if (remo->frameNum == 0) {
    remo->frameOffset[remo->frameNum] = 0;
  } else {
    uint8_t size = sizeof(RemoFrame);
    RemoFrame *cur = _remoFramePtr(-1);
    if (cur->type == TYPE_DATA) {
      size = (uint8_t)(size + cur->dataLen);
    }
    uint8_t offset = (uint8_t)(remo->frameOffset[remo->frameNum-1] + size);
    offset = (uint8_t)((offset+1) & ~0x1); // add padding
    remo->frameOffset[remo->frameNum] = offset;
  }
  remo->frameNum++;
}

int8_t _parseLeader(uint32_t time, uint8_t signal) {
  if (time > 32767) {
    return -1;
  }

  uint16_t *pl = remo->leader;
  if (signal == 1) {
    pl[0] = (uint16_t)time;
    return 0;
  }

  pl[1] = (uint16_t)time;
  for (uint8_t i=0; i<ARRAYSIZE_U8(remoFormat); i++) {
    const RemoFormat *prf = &remoFormat[i];
    uint8_t type = TYPE_UNKNOWN;
    if (prf->leader[0][0] <= pl[0] && pl[0] <= prf->leader[0][1]) {
      if (prf->leader[1][0] == 0) {
        type = TYPE_DATA; // SONY
      } else {
        // NEC or KADENKYO
        if (prf->leader[1][0] <= pl[1] && pl[1] <= prf->leader[1][1]) {
          type = TYPE_DATA;
        } else if (prf->repeater[0] <= pl[1] && pl[1] <= prf->repeater[1]) {
          type = TYPE_REPEATER;
        }
      }
    }
    if (type == TYPE_UNKNOWN) {
      continue;
    }
    assert(remo->format == FMT_UNKNOWN || remo->format == i + 1);
    remo->format = (uint8_t)(i + 1); // FMT_NEC, FMT_KADENKYO or FMT_SONY
    _incrementRemoFrame();
    RemoFrame *cur = _remoFramePtr(-1);
    cur->type = (unsigned)(type & 0x3);
    cur->dataLen = 0;
    cur->time = 0;
    return 1;
  }
  return -2;
}

// @return <0: error 0:keep reading 1:end data 2:want next leader
int8_t _parseData(uint32_t time, uint8_t signal) {
  const RemoFormat *prf = &remoFormat[remo->format - 1];
  const uint8_t timingEdge = (prf->leader[1][0] == 0) ? 0 : 1;
  uint8_t goodBreak = (remo->readPos == 0);
  RemoFrame *cur = _remoFramePtr(-1);
  cur->time = (uint16_t)(cur->time + (time >> 3));
  if (signal == timingEdge) {
    if (time < prf->data0[0])
      return -1;
    if (time <= prf->data0[1]) {
      remo->sumT = (uint16_t)(remo->sumT + (time >> 3));
      remo->numT++;
      return 0;
    }
    if (goodBreak) {
      if (timingEdge == 0) {
        // SONY
        if (prf->leader[0][1] < time) {
          return 2;
        }
      }
    }
    return -2;
  } else {
    if (prf->data0[0] <= time && time <= prf->data0[1]) {
      _storeData(0, time);
      return 0;
    }
    if (prf->data1[0] <= time && time <= prf->data1[1]) {
      _storeData(1, time);
      return 0;
    }
    if (goodBreak) {
      if (timingEdge == 1) {
        // NEC or KADENKYO
        if (prf->leader[0][1] < time) {
          return 2;
        }
      }
    }
  }
  return 0;
}

void _storeData(uint8_t v, uint32_t time) {
  RemoFrame *cur = _remoFramePtr(-1);
  const RemoFormat *prf = &remoFormat[remo->format - 1];
  remo->sumT = (uint16_t)(remo->sumT + (time >> 3));
  if (v == 0) {
      remo->readByte = (uint8_t)(remo->readByte & (uint8_t)(~(1 << (7 - remo->readPos))));
      remo->numT = (uint16_t)(remo->numT + prf->t.data0);
  } else {
      remo->readByte = (uint8_t)(remo->readByte | (uint8_t)(1 << (7 - remo->readPos)));
      remo->numT = (uint16_t)(remo->numT + prf->t.data1);
  }
  remo->readPos++;
  if (remo->readPos == 8) {
    cur->data[cur->dataLen++] = remo->readByte;
    remo->readPos = 0;
  }
}

void _applySameData(void) {
  if (remo->frameNum <= 1)
    return;
  RemoFrame *cur = _remoFramePtr(-1);
  if (cur->type == TYPE_DATA) {
    RemoFrame *first = _remoFramePtr(0);
    if (cur->dataLen == first->dataLen &&
        memcmp(cur->data, first->data, cur->dataLen) == 0) {
      cur->type = TYPE_SAME_AS_FIRST;
    }
  }
}
