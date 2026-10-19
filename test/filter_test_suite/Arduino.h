#pragma once
// Minimal Arduino.h stub for Linux test build
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

// Stream base class — used by ConfigSerializer
class Stream {
public:
    virtual int  available() = 0;
    virtual int  read()      = 0;

    // print overloads used by ConfigSerializer
    virtual size_t print(char c)            { return write((uint8_t)c); }
    virtual size_t print(const char* s)     { size_t n=0; while(*s) n+=print(*s++); return n; }
    virtual size_t print(int32_t v, int b=10)  { char t[32]; snprintf(t,32,(b==16)?"%x":"%d",v); return print(t); }
    virtual size_t print(uint32_t v, int b=10) { char t[32]; snprintf(t,32,(b==16)?"%x":"%u",v); return print(t); }
    virtual size_t print(float v, int d=2)  { char t[32]; snprintf(t,32,"%.*f",d,(double)v); return print(t); }
    virtual size_t print(double v, int d=6) { char t[32]; snprintf(t,32,"%.*f",d,v); return print(t); }

    virtual size_t write(uint8_t c) = 0;

    virtual ~Stream() {}
};
