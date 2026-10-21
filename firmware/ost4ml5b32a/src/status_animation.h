#pragma once
#define SJ_FRAME_MS 20
#define SJ_FRAMES 120
struct sj_status { int transport, profile, battery, layer; bool caps; };
static unsigned sj_ease(unsigned x) {
    if (x >= 256) return 256;
    return x*x*(768-2*x)/65536;
}
static struct led_rgb sj_mix(struct led_rgb a, struct led_rgb b, unsigned t) {
    return (struct led_rgb){
        .r=(a.r*(256-t)+b.r*t+128)/256,
        .g=(a.g*(256-t)+b.g*t+128)/256,
        .b=(a.b*(256-t)+b.b*t+128)/256};
}
static struct led_rgb sj_rainbow(unsigned phase) {
    static const struct led_rgb wheel[6] = {
        {.r=16}, {.r=16,.g=16}, {.g=16},
        {.g=16,.b=16}, {.b=16}, {.r=16,.b=16}};
    phase %= 1536;
    return sj_mix(wheel[phase/256], wheel[(phase/256+1)%6], sj_ease(phase%256));
}
static void sj_render(struct led_rgb p[4], const struct sj_status *s, unsigned frame) {
    static const struct led_rgb colors[5] = {
        {.b=16}, {.g=12,.b=12}, {.g=16}, {.r=12,.g=7}, {.r=12,.b=12}};
    const struct led_rgb black={0};
    for (int i=0;i<4;i++) p[i]=black;
    if (frame >= SJ_FRAMES) return;
    struct led_rgb status[4] = {
        s->transport==1 ? (struct led_rgb){.g=12,.b=12} :
        s->transport==2 ? (struct led_rgb){.g=16} : (struct led_rgb){.r=12,.g=5},
        colors[(s->profile>=0 && s->profile<5)?s->profile:0],
        colors[(s->layer>=0 && s->layer<5)?s->layer:4],
        s->caps ? (struct led_rgb){.r=14,.b=10} : black};
    int count=s->battery<=0 ? 0 : (s->battery+24)/25;
    if (count>4) count=4;
    struct led_rgb battery=s->battery<=20 ? (struct led_rgb){.r=12} :
        s->battery<=50 ? (struct led_rgb){.r=10,.g=6} : (struct led_rgb){.g=12};
    for (int i=0;i<4;i++) {
        /* Moving rainbow across all four pixels, spaced by 60 degrees. */
        struct led_rgb c=sj_rainbow(frame*16+i*256);
        struct led_rgb gauge=s->battery<0 ? (struct led_rgb){.b=3} :
            (i<count ? battery : black);
        if (frame>=45) c=sj_mix(c,gauge,sj_ease((frame-45)*256/15));
        if (frame>=75) c=sj_mix(c,status[i],sj_ease((frame-75)*256/15));
        unsigned envelope=frame<15 ? sj_ease(frame*256/15) :
            frame>=100 ? sj_ease((119-frame)*256/19) : 256;
        p[i]=sj_mix(black,c,envelope);
    }
}