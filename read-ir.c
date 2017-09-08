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
    {{MARGINL(4*TSONY), MARGINR(4*TSONY)}, {0, 0}},
    {MARGINL(1*TSONY), MARGINR(1*TSONY)},
    {MARGINL(2*TSONY), MARGINR(2*TSONY)},
    {0, 0},
    {4+1, 1, 2, 0},
  },
};




int8_t _parseLeader(uint32_t time, uint8_t signal);
int8_t _parseData(uint32_t time, uint8_t signal);
int8_t _storeData(uint8_t v, uint32_t time);
void _applySameData(void);

Remo *remo;
RemoFrame *remoFrame;
RemoWork *work;

void initRemo(void *buffer, uint8_t buffSize, void *workBuff) {
  if (workBuff != NULL) {
    memset(workBuff, 0, sizeof(RemoWork));
  }
  work = workBuff;

  memset(buffer, 0, buffSize);
  remo = (Remo*)buffer;
  remoFrame = (RemoFrame*)(buffer + sizeof(Remo));
  work->frameBuffSize = (uint8_t)(buffSize - sizeof(Remo));
}


// @return <0:error 0:keep reading 1:end
int8_t parseRemo(uint32_t time, uint8_t signal) {
  int8_t ret;
  if (work->readState == 0) {
    ret = _parseLeader(time, signal);
    if (ret < 0)
      return (int8_t)(-10+ret);
    if (ret == 1) {
      work->readState = 1;
      RemoFrame *cur = remoFramePtr(-1);
      const RemoFormat *prf = &remoFormat[remo->format - 1];
      remo->frameTime = (uint32_t)(work->leader[0] + work->leader[1]);
      cur->time = (uint16_t)(remo->frameTime >> 3);
      remo->t += time;
      if (cur->type == TYPE_DATA)
        work->numT = (uint16_t)(work->numT + prf->t.leader);
      else // == TYPE_REPEATER
        work->numT = (uint16_t)(work->numT + prf->t.repeater);
    }
  } else if (work->readState == 1) {
    int8_t ret = _parseData(time, signal);
    if (ret < 0)
      return (int8_t)(-20+ret);
    else if (ret == 1)
      return 1;
    else if (ret == 2) {
      _applySameData();
      work->readState = 0;
    }
  }
  return 0;
}

void outRemo() {
  static const char *formatStr[] = {"UNKNOWN", "NEC", "KADENKYO", "SONY"};
  static const char *typeStr[] = {"UNKNOWN", "DATA", "SAME", "REP"};
  RemoFrame *p;
  uint32_t averageT = (uint32_t)(work->numT != 0 ? remo->t / work->numT : 0);
  uint32_t averageFrame=0;
  uint16_t i;
  for (i=0; i<remo->frameNum-1; i++) {
    p = remoFramePtr((int8_t)i);
    averageFrame += p->time;
  }
  if (i>0)
    averageFrame = averageFrame / i;
  printf("avarage     : %d, %d (T, frame) [us]\n", averageT, averageFrame << 3);
  printf("format      : %s\n", formatStr[remo->format]);
  printf("frameNum    : %d\n", remo->frameNum);
  printf("frameOffset :");
  for (int i=0; i<(int)sizeof(remo->frameOffset); i++) {
    printf(" %02x", remo->frameOffset[i]);
  }
  printf("\n");
  printf("work        : %d %d %d %d {%04x, %04x}\n",
         remo->frameTime, remo->t, work->numT,
         work->readState,
         work->leader[0], work->leader[1]);
  printf("frames      : [");
  for (int8_t i=0; i<remo->frameNum; i++) {
    RemoFrame *p = remoFramePtr(i);
    uint32_t time = (uint32_t)(p->time << 3);
    if (i>0)
      printf(", ");
    printf("%s", typeStr[p->type]);
    if (p->type == TYPE_DATA) {
      printf("[");
      uint8_t len = (uint8_t)((p->dataBits + 7) >> 3);
      for (uint8_t j=0; j<len; j++) {
        if (j>0)
          printf(" ");
        printf("%02x", p->data[j]);
      }
      printf("]:%d", p->dataBits);
    }
    printf("(%d[us])", time);
  }
  printf("]\n");
}




RemoFrame *remoFramePtr(int8_t index) {
  //uint8_t i;
  uint8_t *ptr = (uint8_t*)remoFrame;
  if (index == -1) {
    index = (int8_t)(remo->frameNum - 1);
  }
  return (RemoFrame*)(ptr + remo->frameOffset[index]);
}

int8_t incrementRemoFrame(void) {
  if (remo->frameNum == 0) {
    remo->frameOffset[remo->frameNum] = 0;
  } else {
    uint8_t size = sizeof(RemoFrame);
    RemoFrame *cur = remoFramePtr(-1);
    if (cur->type == TYPE_DATA) {
      uint8_t len = (uint8_t)((cur->dataBits + 7) >> 3);
      size = (uint8_t)(size + len);
    }
    uint8_t offset = (uint8_t)(remo->frameOffset[remo->frameNum-1] + size);
    offset = (uint8_t)((offset+1) & ~0x1); // add padding
    remo->frameOffset[remo->frameNum] = offset;
    if (work->frameBuffSize < offset + sizeof(RemoFrame)) {
      return -1;
    }
  }
  remo->frameNum++;
  return 0;
}

int8_t _parseLeader(uint32_t time, uint8_t signal) {
  if (time > 32767) {
    return -1;
  }

  uint16_t *pl = work->leader;
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
    if (incrementRemoFrame() != 0) {
      return -2;
    }
    RemoFrame *cur = remoFramePtr(-1);
    cur->type = (unsigned)(type & 0x3);
    cur->dataBits = 0;
    return 1;
  }
  return -3;
}

// @return <0: error 0:keep reading 1:end data 2:want next leader
int8_t _parseData(uint32_t time, uint8_t signal) {
  const RemoFormat *prf = &remoFormat[remo->format - 1];
  const uint8_t timingEdge = (prf->leader[1][0] == 0) ? 0 : 1;
  RemoFrame *cur = remoFramePtr(-1);
  remo->frameTime += time;
  cur->time = (uint16_t)(remo->frameTime >> 3);
  if (signal == timingEdge) {
    if (time < prf->data0[0])
      return -1;
    if (time <= prf->data0[1]) {
      remo->t += time;
      work->numT++;
      return 0;
    }
    if (timingEdge == 0) {
      // SONY
      if (prf->leader[0][1] < time) {
        return 2;
      }
    }
    return -2;
  } else {
    if (prf->data0[0] <= time && time <= prf->data0[1]) {
      return _storeData(0, time) == 0 ? 0 : -3;
    }
    if (prf->data1[0] <= time && time <= prf->data1[1]) {
      return _storeData(1, time) == 0 ? 0 : -3;
    }
    if (timingEdge == 1) {
      // NEC or KADENKYO
      if (prf->leader[0][1] < time) {
        return 2;
      }
    }
    return -4;
  }
  return 0;
}

int8_t _storeData(uint8_t v, uint32_t time) {
  RemoFrame *cur = remoFramePtr(-1);
  const RemoFormat *prf = &remoFormat[remo->format - 1];
  uint8_t *p = &cur->data[cur->dataBits >> 3];
  uint8_t bit = (uint8_t)(1 << (7 - (cur->dataBits & 0x7)));

  if (((uint8_t*)remoFrame + work->frameBuffSize) <= p)
    return -1;

  remo->t += time;
  if (v == 0) {
    *p = (uint8_t)((*p) & (uint8_t)(~bit));
    work->numT = (uint16_t)(work->numT + prf->t.data0);
  } else {
    *p = (uint8_t)((*p) | (uint8_t)bit);
    work->numT = (uint16_t)(work->numT + prf->t.data1);
  }
  *p = (uint8_t)((*p) & (uint8_t)(~(bit-1)));
  cur->dataBits++;
  return 0;
}

void _applySameData(void) {
  if (remo->frameNum <= 1)
    return;
  RemoFrame *cur = remoFramePtr(-1);
  if (cur->type == TYPE_DATA) {
    RemoFrame *first = remoFramePtr(0);
    if (cur->dataBits == first->dataBits) {
      uint8_t len = (uint8_t)((cur->dataBits + 7) >> 3);
      if (memcmp(cur->data, first->data, len) == 0) {
        cur->type = TYPE_SAME_AS_FIRST;
      }
    }
  }
}
