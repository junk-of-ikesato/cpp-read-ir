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
#define TYPE_SEPARATOR 4

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
  uint8_t type; // TYPE_xxx
  union {
    struct {
      // data length
      uint8_t len;
      // size are greater than or equal to 1
      uint8_t data[1];
    } data;

    // unit is 1us * 8 = 8us.
    // if time is 200ms then 'separator' value are 25000 (== 200*1000/8).
    uint8_t separator[2]; // == uint16 separator; but using uint8_t for struct padding issue
  };
} RemoData;

typedef struct Remo_t {
  uint16_t averageT; // [us]
  uint16_t averageSeparator;  // [10us]
  uint8_t format; // 0:unknown 1:nec 2:kadenkyo 3:sony
  uint8_t dataNum;
  uint8_t dataOffset[10];

  // work variables
  uint8_t readByte;
  uint8_t readPos;
  uint8_t readState; // 0:unknown 1:parsing data
  uint16_t leader[2];
} Remo;


RemoData *_remoDataPtr(int8_t index);
void _incrementRemoData(void);
int8_t _parseLeader(uint32_t time, uint8_t signal);
int8_t _parseData(uint32_t time, uint8_t signal);
void _storeData(void);
void _storeSeparator(uint32_t time);

Remo *remo;
RemoData *remoData;

void initRemo(void *buffer, uint8_t size) {
  memset(buffer, 0, size);
  remo = (Remo*)buffer;
  remoData = (RemoData*)(buffer + sizeof(Remo));
  //remo->dataNum = 1;
}


// @return <0:error 0:keep reading 1:end
int8_t parseRemo(uint32_t time, uint8_t signal) {
  int8_t ret;
  if (remo->readState == 0) {
    ret = _parseLeader(time, signal);
    if (ret < 0)
      return -1;
    if (ret == 1)
      remo->readState = 1;
  } else if (remo->readState == 1) {
    int8_t ret = _parseData(time, signal);
    if (ret < 0)
      return -2;
    else if (ret == 1)
      return 1;
    else if (ret == 2) {
      remo->readState = 0;
    }
  }
  return 0;
}

void outRemo() {
  const char *formatStr[] = {"UNKNOWN", "NEC", "KADENKYO", "SONY"};
  const char *typeStr[] = {"UNKNOWN", "DATA", "SAME", "REP", "SEP"};
  printf("avarage    : %d, %d (T, separator)\n", remo->averageT, remo->averageSeparator);
  printf("format     : %s\n", formatStr[remo->format]);
  printf("dataNum    : %d\n", remo->dataNum);
  printf("dataOffset :");
  for (int i=0; i<(int)sizeof(remo->dataOffset); i++) {
    printf(" %02x", remo->dataOffset[i]);
  }
  printf("\n");
  printf("work       : %02x %d %d {%04x, %04x}\n",
         remo->readByte, remo->readPos, remo->readState,
         remo->leader[0], remo->leader[1]);
  printf("data       : [");
  for (int8_t i=0; i<remo->dataNum; i++) {
    RemoData *p = _remoDataPtr(i);
    if (i>0)
      printf(", ");
    printf("%s", typeStr[p->type]);
    if (p->type == TYPE_DATA) {
      printf("[");
      for (uint8_t j=0; j<p->data.len; j++) {
        if (j>0)
          printf(" ");
        printf("%02x", p->data.data[j]);
      }
      printf("]");
    } else if (p->type == TYPE_SEPARATOR) {
      uint32_t time = (uint32_t)((p->separator[0] + (p->separator[1]<<8))<<3);
      printf("(%d)", time);
    }
  }
  printf("]\n");
}




RemoData *_remoDataPtr(int8_t index) {
  //uint8_t i;
  uint8_t *ptr = (uint8_t*)remoData;
  if (index == -1) {
    index = (int8_t)(remo->dataNum - 1);
  }
  return (RemoData*)(ptr + remo->dataOffset[index]);
}

void _incrementRemoData(void) {
  if (remo->dataNum == 0) {
    remo->dataOffset[remo->dataNum] = 0;
  } else {
    uint8_t size = sizeof(RemoData);
    RemoData *cur = _remoDataPtr(-1);
    if (cur->type == TYPE_DATA) {
      size = (uint8_t)(size + cur->data.len);
    }
    remo->dataOffset[remo->dataNum] = (uint8_t)(remo->dataOffset[remo->dataNum-1] + size);
  }
  remo->dataNum++;
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
    printf("format => %d\n", remo->format);
    _incrementRemoData();
    RemoData *cur = _remoDataPtr(-1);
    cur->type = type;
    return 1;
  }
  return -2;
}

// @return <0: error 0:keep reading 1:end data 2:want next leader
int8_t _parseData(uint32_t time, uint8_t signal) {
  const RemoFormat *prf = &remoFormat[remo->format - 1];
  const uint8_t timingEdge = (prf->leader[1][0] == 0) ? 0 : 1;
  uint8_t goodBreak = (remo->readPos == 0);
  if (signal == timingEdge) {
    if (time < prf->data0[0])
      return -1;
    if (time <= prf->data0[1])
      return 0;
    if (goodBreak) {
      if (timingEdge == 1) {
        // NEC or KADENKYO
        if (prf->leader[0][0] <= time && time <= prf->leader[0][1]) {
          remo->leader[0] = (uint16_t)time;
          return 2;
        }
      } else {
        // SONY
        if (prf->leader[0][1] < time) {
          _storeSeparator(time);
          return 2;
        }
      }
    }
    return -2;
  } else {
    if (prf->data0[0] <= time && time <= prf->data0[1]) {
      remo->readByte = (uint8_t)(remo->readByte & (uint8_t)(~(1 << (7 - remo->readPos))));
      _storeData();
      return 0;
    }
    if (prf->data1[0] <= time && time <= prf->data1[1]) {
      remo->readByte = (uint8_t)(remo->readByte | (uint8_t)(1 << (7 - remo->readPos)));
      _storeData();
      return 0;
    }
    if (goodBreak) {
      if (timingEdge == 1) {
        // NEC or KADENKYO
        if (prf->leader[0][1] < time) {
          _storeSeparator(time);
          return 2;
        }
      }
    }
  }
  return 0;
}

void _storeData(void) {
  remo->readPos++;
  if (remo->readPos == 8) {
    RemoData *cur = _remoDataPtr(-1);
    cur->data.data[cur->data.len++] = remo->readByte;
    remo->readPos = 0;
  }
}

void _storeSeparator(uint32_t time) {
  _incrementRemoData();
  RemoData *cur = _remoDataPtr(-1);
  cur->type = TYPE_SEPARATOR;
  uint16_t sep = (uint16_t)(time >> 3);
  cur->separator[0] = (uint8_t)(sep & 0xff);
  cur->separator[1] = (uint8_t)((sep >> 8) & 0xff);
}
