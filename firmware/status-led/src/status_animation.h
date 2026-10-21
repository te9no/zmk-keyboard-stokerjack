#pragma once
#define SJ_FRAMES 48
struct sj_status { int transport, profile, battery, layer; bool caps; };
/* Raw LED values are capped at 16/255; no full-brightness flashes. */
static void sj_render(struct led_rgb p[4], const struct sj_status *s, unsigned frame) {
    static const struct led_rgb colors[5] = {
        {.b=16}, {.g=12,.b=12}, {.g=16}, {.r=12,.g=7}, {.r=12,.b=12}
    };
    for (int i=0;i<4;i++) p[i]=(struct led_rgb){0};
    if (frame >= SJ_FRAMES) return;
    struct led_rgb profile=colors[(s->profile>=0 && s->profile<5)?s->profile:0];
    struct led_rgb connection=s->transport==1 ? (struct led_rgb){.g=12,.b=12} :
        s->transport==2 ? (struct led_rgb){.g=16} : (struct led_rgb){.r=12,.g=5};
    if (frame<8) { p[frame/2]=profile; return; }
    if (frame<12) { for(int i=0;i<4;i++) p[i]=connection; return; }
    if (frame<28) {
        if (s->battery<0) { p[(frame-12)/4]=(struct led_rgb){.b=3}; return; }
        int count=s->battery<=0 ? 0 : (s->battery+24)/25;
        if (count>4) count=4;
        if (s->battery<=20) {
            if ((frame/3)%2==0) for(int i=0;i<4;i++) p[i]=(struct led_rgb){.r=12};
        } else {
            struct led_rgb c=s->battery<=50 ? (struct led_rgb){.r=10,.g=6} : (struct led_rgb){.g=12};
            for(int i=0;i<count && i<=(int)(frame-12)/3;i++) p[i]=c;
        }
        return;
    }
    /* Status row: connection, selected BLE profile, layer, Caps Lock. */
    p[0]=connection; p[1]=profile;
    p[2]=colors[(s->layer>=0 && s->layer<5)?s->layer:4];
    p[3]=s->caps ? (struct led_rgb){.r=14,.b=10} : (struct led_rgb){0};
    if(frame>=36) for(int i=0;i<4;i++) {
        p[i].r=p[i].r*(47-frame)/11; p[i].g=p[i].g*(47-frame)/11; p[i].b=p[i].b*(47-frame)/11;
    }
}
