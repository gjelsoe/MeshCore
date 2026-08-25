#pragma once

#include <Mesh.h>
#include <Arduino.h>

#ifdef NRF52_PLATFORM
#define CLOCK_MAGIC_NUM        0xAA55CC33
#define RTC_TIME_MIN           1772323200  // 1 Mar 2026

extern uint32_t persistent_magic;
extern uint32_t persistent_time;
#endif

class VolatileRTCClock : public mesh::RTCClock {
  uint32_t base_time;
  uint64_t accumulator;
  unsigned long prev_millis;
  bool _synced;

public:
  VolatileRTCClock() {
#ifdef NRF52_PLATFORM
    if (persistent_magic == CLOCK_MAGIC_NUM && persistent_time >= RTC_TIME_MIN) {
      base_time = persistent_time;
      _synced = true;    // restored from a previously-real sync, not the fallback seed
    } else {
      base_time = RTC_TIME_MIN;
      _synced = false;   // running on the hardcoded fallback seed until a real sync happens
    }
#else
    base_time = 1715770351;
    _synced = false;     // no persistence on this platform -- always starts unsynced
#endif

    accumulator = 0;
    prev_millis = millis();
  }

  uint32_t getCurrentTime() override { return base_time + accumulator/1000; }

  void setCurrentTime(uint32_t time) override {
    base_time = time;
    accumulator = 0;
    prev_millis = millis();
    _synced = true;   // explicit set = GPS fix or manual "time set" -- real sync

#ifdef NRF52_PLATFORM
    persistent_magic = CLOCK_MAGIC_NUM;
    persistent_time = time;
#endif
  }

  void tick() override {
    unsigned long now = millis();
    accumulator += (now - prev_millis);
    prev_millis = now;

#ifdef NRF52_PLATFORM
    // Only persist once genuinely synced -- otherwise a fallback-seeded clock
    // (never actually synced) would look "restored from a real sync" on the
    // next reboot, since persistent_time only ever increases from RTC_TIME_MIN.
    if (_synced) {
      persistent_magic = CLOCK_MAGIC_NUM;
      persistent_time = getCurrentTime();
    }
#endif
  }

  bool isTimeValid() override { return _synced; }
};

class ArduinoMillis : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override { return millis(); }
};

class StdRNG : public mesh::RNG {
public:
  void begin(long seed) { randomSeed(seed); }
  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = (::random(0, 256) & 0xFF);
    }
  }
};