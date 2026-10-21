#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
struct led_rgb { uint8_t r, g, b; };
#include "ost_protocol.h"
#include "status_animation.h"
static int levels[192], count, waits;
static void high(void) { levels[count++] = 1; }
static void low(void) { levels[count++] = -1; }
static void midpoint(void) { levels[count++] = 0; }
static void delay(unsigned us) { assert(us == 3); waits++; }
int main(void) {
    struct led_rgb p[4] = {{.r=1,.g=0x55,.b=0x80}, {.r=0xfe,.g=0xaa,.b=0x01},
                           {.r=0,.g=0,.b=0}, {.r=0xff,.g=0xff,.b=0xff}};
    const uint8_t expected[] = {0x80,0x55,1,1,0xaa,0xfe,0,0,0,0xff,0xff,0xff};
    uint8_t bytes[12]; ost_encode(p, bytes);
    for (int i=0;i<12;i++) assert(bytes[i] == expected[i]);
    OST_SEND_BYTES(bytes, high, low, midpoint, delay);
    assert(count == 192 && waits == 192);
    for (int bit=0;bit<96;bit++) {
        assert(levels[2*bit] == ((expected[bit/8] & (0x80 >> (bit%8))) ? 1 : -1));
        assert(levels[2*bit+1] == 0);
    }
    struct sj_status s = {.transport=2,.profile=4,.battery=100,.layer=3,.caps=true};
    for (int bat=-1;bat<=100;bat++) for(int frame=0;frame<=SJ_FRAMES;frame++) {
        s.battery=bat; sj_render(p,&s,frame); ost_encode(p,bytes);
        for(int i=0;i<12;i++) { assert(bytes[i]<=16); if(frame>=SJ_FRAMES-1) assert(bytes[i]==0); }
    }
    assert(SJ_FRAMES * SJ_FRAME_MS == 2400);
    for (int bat=-1;bat<=100;bat++) {
        s.battery=bat;
        uint8_t previous[12]={0};
        for (unsigned f=0;f<=SJ_FRAMES;f++) {
            sj_render(p,&s,f); ost_encode(p,bytes);
            for (int i=0;i<12;i++) {
                int delta=(int)bytes[i]-previous[i];
                assert(delta>=-3 && delta<=3);
                previous[i]=bytes[i];
            }
            if (f>=15 && f<45) for(int i=0;i<4;i++)
                assert(p[i].r || p[i].g || p[i].b);
        }
    }
    puts("PASS: protocol, brightness/blackout, 2400ms, four lit pixels, bounded frame transitions");
}
