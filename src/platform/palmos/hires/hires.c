/* NIO Config on a high-density colour Palm (Palm OS 5, such as the Sony Clie TG50): the same settings as the standard
 * form (config.c), shown off. Three pages:
 *
 *   LINK    the route from the Palm to the Internet, drawn and animated while the link is up; the transport to use
 *           (Network: the NIO inside NioLib over the Palm's own NetLib; Springboard; Serial; USB), saved when tapped;
 *           NetLib's link, address and counters; Connect (dial the Network preferences' service) and Hang up.
 *   TEST    timed probes through NioLib, as a FujiNet app makes them: the NIO's info, TCP connections to game
 *           servers, an HTTP fetch, each a bar of its time.
 *   DEVICE  the Palm itself: battery and free memory as gauges, its OS, maker and screen.
 *
 * Each action answers with a short sound, and so does a link that comes up or drops while LINK is showing.
 *
 * config.c's PilotMain runs this when hires_capable() says the screen allows; everything else is NioLib's own API. */
#include <PalmOS.h>
#include <NetMgr.h>
#include "NioLibClient.h"
#define PALM_FONT_WIDTHS
#include "ui.h"

#define TAB_H   30
#define PAGE_Y  38
#define PAGE_H  (SCREEN_H - TAB_H - PAGE_Y)

/* The pages, left to right. A build with H2H_DIR (makefiles/palmos.mk) adds H2H: the head-to-head speed test. */
#define P_LINK 0
#define P_TEST 1
#ifdef NIO_H2H
#define P_H2H 2
#define P_DEVICE 3
#define NPAGES 4
#else
#define P_DEVICE 2
#define NPAGES 3
#endif

static UInt16 ref;
static Boolean open_, loaded;
static NioLibVersion ver;
static UInt32 caps;
static NioLibConfig cfg;
static NioLibNetLibStatus net;
static UInt8 page, anim, pulse;
static const Char *note = "";
static rgb_t note_c;

/* ---------------------------------------------------------------- drawing helpers */

static rgb_t mix(rgb_t a, rgb_t b, UInt8 t)
{
    unsigned long r, g, bl;
    if (t >= 16) return b;
    r = (((a >> 16) & 255) * (16 - t) + ((b >> 16) & 255) * t) / 16;
    g = (((a >> 8) & 255) * (16 - t) + ((b >> 8) & 255) * t) / 16;
    bl = ((a & 255) * (16 - t) + (b & 255) * t) / 16;
    return (r << 16) | (g << 8) | bl;
}

static short width(const char *s, UInt8 font)
{
    short w = 0;
    while (*s) {
        UInt8 c = (UInt8)*s++;
        if (c >= 32 && c <= 126) w += palm_font_w[font][c - 32];
    }
    return w;
}

static void say(short x, short y, const char *s, UInt8 font, rgb_t c)
{
    gfx_text(x, y, s, (UInt8)StrLen(s), font, c);
}

static void say_c(short x, short w, short y, const char *s, UInt8 font, rgb_t c)
{
    say(x + (w - width(s, font)) / 2, y, s, font, c);
}

static void say_r(short right, short y, const char *s, UInt8 font, rgb_t c)
{
    say(right - width(s, font), y, s, font, c);
}

static void vgrad(short x, short y, short w, short h, rgb_t top, rgb_t bottom)
{
    short i, band = h > 16 ? (h + 15) / 16 : 1;
    for (i = 0; i < h; i += band)
        gfx_rect(x, y + i, w, i + band > h ? h - i : band, mix(top, bottom, (UInt8)(i * 16 / (h > 1 ? h : 1))));
}

static void glow(short x, short y, const char *s, UInt8 font, rgb_t c, rgb_t g)
{
    say(x - 1, y, s, font, g); say(x + 1, y, s, font, g);
    say(x, y - 1, s, font, g); say(x, y + 1, s, font, g);
    say(x, y, s, font, c);
}

static void card(short x, short y, short w, short h, rgb_t edge)
{
    vgrad(x, y, w, h, C_BG2, C_BG1);
    gfx_frame(x, y, w, h, edge);
    gfx_rect(x + 1, y + 1, w - 2, 1, mix(edge, C_WHITE, 6));
}

static void button(short x, short y, short w, short h, const char *label, rgb_t c, Boolean hot)
{
    vgrad(x, y, w, h, hot ? mix(c, C_WHITE, 4) : C_BG2, hot ? mix(c, C_BG0, 7) : C_BG1);
    gfx_frame(x, y, w, h, hot ? mix(c, C_WHITE, 6) : C_EDGEHI);
    say_c(x, w, y + (h - F_SMALL_H) / 2 + 1, label, F_SMALL, hot ? C_BG0 : c);
}

/* Integer sine, 0..64 for a quarter turn in 16 steps: enough for gauges and dots. */
static const UInt8 sine16[17] = { 0, 6, 12, 19, 24, 30, 35, 40, 45, 49, 53, 56, 59, 61, 63, 64, 64 };
static short sin64(short a)             /* a: 0..63 is a whole turn */
{
    a &= 63;
    if (a < 16) return sine16[a];
    if (a < 32) return sine16[32 - a];
    if (a < 48) return -(short)sine16[a - 32];
    return -(short)sine16[64 - a];
}
#define cos64(a) sin64((a) + 16)

/* A ring gauge: pct of the way round from the top, in c; the rest dim. */
static void gauge(short cx, short cy, short r, UInt8 pct, rgb_t c, const char *big, const char *label)
{
    short i, k, x, y;
    for (i = 0; i < 64; ++i)
        for (k = 0; k < 5; ++k) {
            x = cx + (short)((long)(r - k) * sin64(i) / 64);
            y = cy - (short)((long)(r - k) * cos64(i) / 64);
            gfx_rect(x, y, 2, 2, i * 100 / 64 < pct ? mix(c, C_WHITE, (UInt8)(k == 0 ? 6 : 0)) : C_EDGE);
        }
    say_c(cx - r, 2 * r, cy - 10, big, F_BIG, C_WHITE);
    say_c(cx - r, 2 * r, cy + 13, label, F_TINY, C_DIM);
}

/* ---------------------------------------------------------------- sound */

/* SYN-ACK WIRE's style on the Palm's speaker: short runs of square-wave notes, a few hundred milliseconds at most,
 * each played to its end before the next. The volume is the Palm's system sound volume, or its game sound volume
 * when system sounds are off; with both off NIO Config is quiet. */
struct note { UInt16 hz; UInt8 ms; };   /* hz 0: a rest; ms 0: the end */
static const struct note s_click[] = { { 1800, 6 }, { 0, 0 } };
static const struct note s_saved[] = { { 659, 40 }, { 784, 40 }, { 1047, 80 }, { 0, 0 } };
static const struct note s_error[] = { { 196, 70 }, { 0, 30 }, { 147, 120 }, { 0, 0 } };
static const struct note s_dial[] = { { 1300, 35 }, { 2100, 35 }, { 1300, 35 }, { 2100, 35 }, { 0, 0 } };
static const struct note s_up[] = { { 523, 50 }, { 659, 50 }, { 784, 50 }, { 1047, 120 }, { 0, 0 } };
static const struct note s_down[] = { { 784, 60 }, { 659, 60 }, { 523, 60 }, { 392, 130 }, { 0, 0 } };
static const struct note s_pass[] = { { 1568, 25 }, { 2093, 45 }, { 0, 0 } };
static const struct note s_fail[] = { { 220, 100 }, { 0, 0 } };
static const struct note s_done[] = { { 523, 60 }, { 659, 60 }, { 784, 60 }, { 1047, 60 }, { 0, 40 }, { 784, 50 },
                                      { 1047, 170 }, { 0, 0 } };
static const struct note s_partly[] = { { 440, 80 }, { 349, 80 }, { 294, 170 }, { 0, 0 } };

static void play(const struct note *n)
{
    SndCommandType c;
    UInt16 amp = PrefGetPreference(prefSysSoundVolume);
    if (!amp) amp = PrefGetPreference(prefGameSoundVolume);
    if (!amp) return;
    for (; n->ms; ++n) {
        if (!n->hz) {
            SysTaskDelay(((Int32)n->ms * SysTicksPerSecond() + 999) / 1000);
            continue;
        }
        c.cmd = sndCmdFreqDurationAmp;
        c.reserved = 0;
        c.param1 = n->hz;
        c.param2 = n->ms;
        c.param3 = amp;
        SndDoCmd(NULL, &c, false);
    }
}

/* ---------------------------------------------------------------- NioLib */

static void lib_open(void)
{
    UInt16 flags = 0;
    if (SysLibFind(nioLibName, &ref)) {
        if (SysLibLoad(nioLibType, nioLibCreator, &ref)) return;
        loaded = true;
    }
    ver.size = sizeof(ver);
    if (NioLibGetVersion(ref, &ver) || NioLibGetCapabilities(ref, &caps) || NioLibOpen(ref)) return;
    open_ = true;
    cfg.size = sizeof(cfg);
    NioLibGetConfig(ref, &cfg, &flags);
}

static void lib_close(void)
{
    UInt16 remaining = 1;
    if (open_ && !NioLibClose(ref, &remaining) && loaded && !remaining) SysLibRemove(ref);
    open_ = false;
}

static Boolean has_netlib(void) { return open_ && ver.abiMinor >= 6 && (caps & nioLibCapNetLib); }

static void read_status(void)
{
    net.size = sizeof(net);
    if (!has_netlib() || NioLibGetNetLibStatus(ref, &net)) MemSet(&net, sizeof(net), 0);
}

static Boolean link_up(void) { return (net.flags & nioNetLibUp) != 0; }

/* LINK's once-a-second look at the link. NioLib's status starts NetLib's idle time over while the link is up, so a
 * link stays up while LINK shows it. One that comes up or drops by itself (dialled by another app, hung up from the
 * Palm's own preferences) gets a sound and a note. */
static Boolean poll_status(void)
{
    Boolean was = link_up();
    read_status();
    if (link_up() == was) return false;
    if (link_up()) { note = "LINK UP"; note_c = C_GREEN; play(s_up); }
    else { note = "LINK DROPPED"; note_c = C_AMBER; play(s_down); }
    return true;
}

static void choose(UInt16 transport)
{
    NioLibConfig c = cfg;
    c.transport = transport;
    if (!open_) { note = "NIOLIB IS NOT INSTALLED"; note_c = C_RED; play(s_error); return; }
    if ((transport == nioTransportNetwork && !has_netlib()) || (transport == nioTransportUSB && !(caps & nioLibCapUSB))) {
        note = "NOT IN THIS NIOLIB"; note_c = C_AMBER; play(s_error); return;
    }
    if (NioLibSetConfig(ref, &c)) { note = "COULD NOT SAVE"; note_c = C_RED; play(s_error); return; }
    cfg = c;
    note = "SAVED: APPS USE IT FROM NOW ON"; note_c = C_GREEN;
    play(s_saved);
}

/* Dial the Network preferences' service: NetLib shows its own progress; the link stays up for its idle time. */
static void connect(void)
{
    UInt16 nref, ifErrs = 0;
    Err err;
    if (SysLibFind("Net.lib", &nref)) { note = "THIS PALM HAS NO NETLIB"; note_c = C_RED; play(s_error); return; }
    note = "DIALING..."; note_c = C_AMBER;
    play(s_dial);
    err = NetLibOpen(nref, &ifErrs);
    if ((err == errNone || err == netErrAlreadyOpen) && !ifErrs) {
        note = "CONNECTED"; note_c = C_GREEN;
        NetLibClose(nref, false);
        play(s_up);
    } else {
        note = "NO CONNECTION: CHECK PREFS > NETWORK"; note_c = C_RED;
        if (err == errNone || err == netErrAlreadyOpen) NetLibClose(nref, true);
        play(s_error);
    }
}

static void hang_up(void)
{
    Err err;
    if (!open_ || !has_netlib()) { note = "NOT IN THIS NIOLIB"; note_c = C_AMBER; play(s_error); return; }
    err = NioLibNetLibHangup(ref);
    if (!err) { note = "HUNG UP"; note_c = C_AMBER; play(s_down); }
    else if (err == nioLibErrBusy) { note = "IN USE: CANNOT HANG UP NOW"; note_c = C_AMBER; play(s_error); }
    else { note = "COULD NOT HANG UP"; note_c = C_RED; play(s_error); }
}

/* ---------------------------------------------------------------- the frame */

static void header(void)
{
    char v[24];
    vgrad(0, 0, SCREEN_W, PAGE_Y - 2, C_BG2, C_BG0);
    gfx_rect(0, PAGE_Y - 3, SCREEN_W, 1, mix(C_MAG, C_BG0, 5));
    glow(8, 7, "FujiNet NIO", F_BIG, C_CYAN, mix(C_MAG, C_BG0, 6));
    if (open_) {
        StrPrintF(v, "NIOLIB %u.%u", ver.abiMajor, ver.abiMinor);
        say_r(SCREEN_W - 8, 8, v, F_SMALL, C_TEXT);
        StrPrintF(v, "BUILD %u", ver.implementation);
        say_r(SCREEN_W - 8, 21, v, F_TINY, C_DIM);
    } else
        say_r(SCREEN_W - 8, 13, "NO NIOLIB", F_SMALL, C_RED);
}

static void tabs(void)
{
#ifdef NIO_H2H
    static const char *const names[NPAGES] = { "LINK", "TEST", "H2H", "DEVICE" };
#else
    static const char *const names[NPAGES] = { "LINK", "TEST", "DEVICE" };
#endif
    UInt8 i;
    short w = SCREEN_W / NPAGES, y = SCREEN_H - TAB_H;
    gfx_rect(0, y - 1, SCREEN_W, 1, C_EDGE);
    for (i = 0; i < NPAGES; ++i) {
        vgrad(i * w, y, w, TAB_H, i == page ? mix(C_CYAN, C_BG2, 9) : C_BG1, C_BG0);
        if (i == page) gfx_rect(i * w + 6, y, w - 12, 3, C_CYAN);
        say_c(i * w, w, y + 8, names[i], F_HEAD, i == page ? C_WHITE : C_DIM);
        if (i) gfx_rect(i * w, y + 4, 1, TAB_H - 8, C_EDGE);
    }
}

static void notice(short y)
{
    gfx_rect(0, y, SCREEN_W, 14, C_BG0);
    say_c(0, SCREEN_W, y, note, F_TINY, note_c);
}

/* ---------------------------------------------------------------- LINK */

#define ROUTE_Y 44
static const char *const hop[4] = { "PALM", "LINK", "GATEWAY", "INTERNET" };

static void route(Boolean up)
{
    UInt8 i, k;
    short x, y = ROUTE_Y + 18, step = (SCREEN_W - 40) / 3;
    rgb_t c = up ? C_GREEN : mix(C_RED, C_BG0, 6);
    gfx_rect(0, ROUTE_Y, SCREEN_W, 46, C_BG0);
    gfx_rect(20, y, SCREEN_W - 40, 2, mix(c, C_BG0, 8));
    if (up)                             /* packets on their way out, and answers coming back */
        for (k = 0; k < 6; ++k) {
            x = 20 + (short)(((k * 47 + anim * 3) % (SCREEN_W - 40)));
            gfx_rect(x, y - 1, 4, 4, k & 1 ? C_CYAN : C_GREEN);
            x = SCREEN_W - 20 - (short)(((k * 53 + anim * 2) % (SCREEN_W - 40)));
            gfx_rect(x, y + 3, 3, 2, C_MAG);
        }
    for (i = 0; i < 4; ++i) {
        rgb_t n = i == 0 ? C_CYAN : i == 1 ? C_VIOLET : i == 2 ? C_AMBER : C_GREEN;
        x = 20 + i * step;
        gfx_rect(x - 9, y - 8, 18, 18, C_BG0);
        gfx_frame(x - 9, y - 8, 18, 18, up || i == 0 ? n : C_EDGE);
        gfx_rect(x - 5, y - 4, 10, 10, up || i == 0 ? mix(n, C_BG0, (UInt8)(up ? pulse / 2 : 8)) : C_BG1);
        say_c(x - 30, 60, y + 14, hop[i], F_TINY, up || i == 0 ? C_TEXT : C_DIM);
    }
}

static void transport_chips(void)
{
    static const char *const names[4] = { "NETWORK", "SPRINGBD", "SERIAL", "USB" };
    static const UInt16 ids[4] = { nioTransportNetwork, nioTransportSpringboard, nioTransportSerial, nioTransportUSB };
    UInt8 i;
    short w = (SCREEN_W - 16 - 9) / 4, y = 108;
    say(8, 94, "TRANSPORT", F_TINY, C_DIM);
    for (i = 0; i < 4; ++i) {
        Boolean avail = open_ && (i == 0 ? has_netlib() : i == 3 ? (caps & nioLibCapUSB) != 0 : true);
        button(8 + i * (w + 3), y, w, 26, names[i], avail ? (i == 0 ? C_GREEN : C_CYAN) : C_DIM,
               open_ && cfg.transport == ids[i]);
    }
}

static void link_status(void)
{
    char t[32];
    Boolean up = (net.flags & nioNetLibUp) != 0;
    short y = 142;
    card(8, y, SCREEN_W - 16, 104, up ? mix(C_GREEN, C_BG0, 6) : C_EDGE);
    say(18, y + 8, "NETLIB", F_TINY, C_DIM);
    glow(18, y + 22, up ? "LINK UP" : (net.flags & nioNetLibPresent) ? "LINK DOWN" : "NO NETLIB", F_BIG,
         up ? C_GREEN : C_RED, mix(up ? C_GREEN : C_RED, C_BG0, 10));
    say(18, y + 52, "ADDRESS", F_TINY, C_DIM);
    say(80, y + 50, net.ip[0] ? net.ip : "-", F_SMALL, C_WHITE);
    say(18, y + 70, "SESSIONS", F_TINY, C_DIM);
    StrPrintF(t, "%u", net.sessions);
    say(80, y + 68, t, F_SMALL, C_CYAN);
    say(130, y + 70, "CONNECTS", F_TINY, C_DIM);
    StrPrintF(t, "%u", net.connects);
    say(190, y + 68, t, F_SMALL, C_CYAN);
    if (net.failures) { StrPrintF(t, "%u FAILED", net.failures); say(220, y + 70, t, F_TINY, C_RED); }
    say(18, y + 87, "IN", F_TINY, C_DIM);
    StrPrintF(t, "%lu B", net.bytesIn);
    say(40, y + 85, t, F_SMALL, C_GREEN);
    say(150, y + 87, "OUT", F_TINY, C_DIM);
    StrPrintF(t, "%lu B", net.bytesOut);
    say(176, y + 85, t, F_SMALL, C_MAG);
    button(SCREEN_W - 114, y + 8, 96, 22, "CONNECT", C_GREEN, false);
    button(SCREEN_W - 114, y + 34, 96, 22, "HANG UP", C_RED, false);
}

static void draw_link(void)
{
    read_status();
    route((net.flags & nioNetLibUp) != 0);
    transport_chips();
    link_status();
    notice(252);
}

/* ---------------------------------------------------------------- TEST */

struct probe { const char *name, *what, *host; UInt16 port; UInt8 kind; UInt8 state; UInt32 ms, bytes; UInt16 http; };
#define P_INFO 0
#define P_TCP  1
#define P_HTTP 2
#define S_IDLE 0
#define S_RUN  1
#define S_OK   2
#define S_FAIL 3
static struct probe probes[] = {
    { "NIO INFO", "FujiDevice GetInfo", 0, 0, P_INFO },
    { "SYN-ACK WIRE", "tcp syn-ack-wire.fly.dev:6502", "syn-ack-wire.fly.dev", 6502, P_TCP },
    { "BOUNCE WORLD", "tcp bouncy.diller.org:9003", "bouncy.diller.org", 9003, P_TCP },
    { "WEB", "http://example.com/", "http://example.com/", 80, P_HTTP },
};
#define NPROBES (sizeof probes / sizeof probes[0])
static char info_text[40];

static void probe_row(UInt8 i)
{
    struct probe *p = &probes[i];
    short y = PAGE_Y + 34 + i * 52, bar;
    char t[40];
    rgb_t c = p->state == S_OK ? C_GREEN : p->state == S_FAIL ? C_RED : p->state == S_RUN ? C_AMBER : C_EDGE;
    card(8, y, SCREEN_W - 16, 46, c);
    say(18, y + 6, p->name, F_HEAD, C_WHITE);
    say(18, y + 24, i == 0 && info_text[0] && p->state == S_OK ? info_text : p->what, F_TINY, C_DIM);
    if (p->state == S_RUN) say_r(SCREEN_W - 18, y + 8, "...", F_SMALL, C_AMBER);
    if (p->state == S_FAIL) say_r(SCREEN_W - 18, y + 8, "FAILED", F_SMALL, C_RED);
    if (p->state == S_OK) {
        if (p->kind == P_HTTP && p->ms)
            StrPrintF(t, "%lu MS  %lu B  %lu KB/S", p->ms, p->bytes, p->bytes * 1000 / p->ms / 1024);
        else
            StrPrintF(t, "%lu MS", p->ms);
        say_r(SCREEN_W - 18, y + 8, t, F_SMALL, C_GREEN);
        bar = (short)(p->ms > 3000 ? SCREEN_W - 40 : (long)(SCREEN_W - 40) * p->ms / 3000);
        gfx_rect(18, y + 38, SCREEN_W - 36, 3, C_BG0);
        vgrad(18, y + 38, bar > 2 ? bar : 2, 3, p->ms < 500 ? C_GREEN : p->ms < 1500 ? C_AMBER : C_RED, C_BG0);
    }
}

static void draw_test(void)
{
    UInt8 i;
    gfx_rect(0, PAGE_Y, SCREEN_W, PAGE_H, C_BG0);
    say(8, PAGE_Y + 8, "PROBES THROUGH NIOLIB", F_TINY, C_DIM);
    button(SCREEN_W - 104, PAGE_Y + 2, 96, 26, "RUN ALL", C_CYAN, true);
    for (i = 0; i < NPROBES; ++i) probe_row(i);
}

static UInt32 ms_since(UInt32 t0)
{
    return (TimGetTicks() - t0) * 1000 / SysTicksPerSecond();
}

static void run_probe(UInt8 i)
{
    struct probe *p = &probes[i];
    fn_handle_t h;
    fn_fuji_info_t info;
    UInt32 t0 = TimGetTicks(), deadline = t0 + 15 * SysTicksPerSecond();
    uint16_t status = 0, got = 0;
    uint32_t length = 0;
    uint8_t flags = 0, r;
    static uint8_t chunk[256];

    p->state = S_RUN;
    gfx_begin(0); probe_row(i); gfx_end();
    p->bytes = 0;
    if (p->kind == P_INFO) {
        r = fn_fuji_get_info(&info);
        if (!r) StrPrintF(info_text, "%s / %s", info.firmware, info.profile);
    } else if (p->kind == P_TCP) {
        r = fn_tcp_open(&h, p->host, p->port);
        if (!r) fn_close(h);
    } else {
        r = fn_open(&h, FN_METHOD_GET, p->host, 0);
        if (!r) {
            do r = fn_info(h, &status, &length, &flags);
            while (r == FN_ERR_NOT_READY && TimGetTicks() < deadline);
            while (!r && TimGetTicks() < deadline) {
                r = fn_read(h, p->bytes, chunk, sizeof chunk, &got, &flags);
                if (r == FN_ERR_NOT_READY) { r = 0; continue; }
                p->bytes += got;
                if (flags & FN_READ_EOF) break;
            }
            fn_close(h);
            p->http = status;
        }
    }
    p->ms = ms_since(t0);
    if (!p->ms) p->ms = 1;
    p->state = r ? S_FAIL : S_OK;
    gfx_begin(0); probe_row(i); gfx_end();
    play(r ? s_fail : s_pass);
}

static void run_all(void)
{
    UInt8 i, r, failed = 0;
    for (i = 0; i < NPROBES; ++i) probes[i].state = S_IDLE;
    note = "OPENING NIOLIB..."; note_c = C_AMBER;
    gfx_begin(1); draw_test(); notice(SCREEN_H - TAB_H - 16); gfx_end();
    r = fn_init();
    if (r) {
        note = "NIOLIB COULD NOT START THE TRANSPORT"; note_c = C_RED;
        play(s_error);
    } else {
        for (i = 0; i < NPROBES; ++i) {
            run_probe(i);
            if (probes[i].state == S_FAIL) ++failed;
        }
        fn_shutdown();
        if (failed) { note = failed == NPROBES ? "ALL FAILED" : "DONE, SOME FAILED"; note_c = C_RED; play(s_partly); }
        else { note = "DONE: ALL PASSED"; note_c = C_GREEN; play(s_done); }
    }
    gfx_begin(0); notice(SCREEN_H - TAB_H - 16); gfx_end();
}

/* ---------------------------------------------------------------- DEVICE */

static void fourcc(UInt32 v, char *out)
{
    out[0] = (char)(v >> 24); out[1] = (char)(v >> 16); out[2] = (char)(v >> 8); out[3] = (char)v; out[4] = 0;
}

static void draw_device(void)
{
    UInt32 rom = 0, company = 0, device = 0, freeB = 0, maxB = 0;
    UInt8 pct = 0;
    Boolean plugged = false;
    char t[40], a[6], b[6];
    short y = PAGE_Y + 8;
    gfx_rect(0, PAGE_Y, SCREEN_W, PAGE_H, C_BG0);
    SysBatteryInfo(false, NULL, NULL, NULL, NULL, &plugged, &pct);
    MemHeapFreeBytes(0, &freeB, &maxB);
    StrPrintF(t, "%u%%", pct);
    gauge(84, y + 62, 54, pct, pct > 30 ? C_GREEN : C_RED, t, plugged ? "BATTERY, CHARGING" : "BATTERY");
    StrPrintF(t, "%luK", freeB / 1024);
    gauge(236, y + 62, 54, (UInt8)(freeB > 4096UL * 1024 ? 100 : freeB * 100 / (4096UL * 1024)), C_CYAN, t,
          "FREE DYNAMIC HEAP");
    FtrGet(sysFtrCreator, sysFtrNumROMVersion, &rom);
    FtrGet(sysFtrCreator, sysFtrNumOEMCompanyID, &company);
    FtrGet(sysFtrCreator, sysFtrNumOEMDeviceID, &device);
    fourcc(company, a);
    fourcc(device, b);
    y += 136;
    card(8, y, SCREEN_W - 16, 100, C_EDGE);
    say(18, y + 8, "PALM OS", F_TINY, C_DIM);
    StrPrintF(t, "%u.%u", (UInt16)sysGetROMVerMajor(rom), (UInt16)sysGetROMVerMinor(rom));
    say(110, y + 6, t, F_SMALL, C_WHITE);
    say(18, y + 26, "MAKER / DEVICE", F_TINY, C_DIM);
    StrPrintF(t, "%s / %s", a, b);
    say(110, y + 24, t, F_SMALL, C_WHITE);
    say(18, y + 44, "SCREEN", F_TINY, C_DIM);
    say(110, y + 42, "320 x 320, 16-BIT", F_SMALL, C_WHITE);
    say(18, y + 62, "CPU", F_TINY, C_DIM);
    say(110, y + 60, rom >= sysMakeROMVersion(5, 0, 0, 0, 0) ? "ARM, 68K APPS IN PACE" : "68K", F_SMALL, C_WHITE);
    say(18, y + 80, "NETWORK", F_TINY, C_DIM);
    say(110, y + 78, has_netlib() ? "NETLIB + NIO IN NIOLIB" : "-", F_SMALL, C_GREEN);
}

#ifdef NIO_H2H
#include "h2h_page.c"
#endif

/* ---------------------------------------------------------------- the app */

static void draw_all(void)
{
    gfx_begin(1);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_BG0);
    header();
    if (page == P_LINK) draw_link();
    else if (page == P_TEST) draw_test();
#ifdef NIO_H2H
    else if (page == P_H2H) draw_h2h();
#endif
    else draw_device();
    tabs();
    gfx_end();
}

static void tap(short x, short y)
{
    short w;
    if (y >= SCREEN_H - TAB_H) {
        page = (UInt8)(x * NPAGES / SCREEN_W);
        note = "";
        play(s_click);
        draw_all();
        return;
    }
    if (page == P_LINK) {
        w = (SCREEN_W - 16 - 9) / 4;
        if (y >= 108 && y < 134) {
            UInt8 i = (UInt8)((x - 8) / (w + 3));
            static const UInt16 ids[4] = { nioTransportNetwork, nioTransportSpringboard, nioTransportSerial,
                                           nioTransportUSB };
            if (i < 4) choose(ids[i]);
        } else if (x >= SCREEN_W - 114 && y >= 150 && y < 172) {
            connect();
        } else if (x >= SCREEN_W - 114 && y >= 176 && y < 198) {
            hang_up();
        } else
            return;
        draw_all();
    } else if (page == P_TEST && y < PAGE_Y + 30 && x >= SCREEN_W - 104) {
        play(s_click);
        run_all();
    }
#ifdef NIO_H2H
    else if (page == P_H2H)
        h2h_tap(x, y);
#endif
}

Boolean hires_capable(void)
{
    UInt32 version = 0, density = 0, depths = 0;
    if (FtrGet(sysFtrCreator, sysFtrNumWinVersion, &version) || version < 4) return false;
    if (WinScreenGetAttribute(winScreenDensity, &density) || density != kDensityDouble) return false;
    if (WinScreenMode(winScreenModeGetSupportedDepths, NULL, NULL, &depths, NULL)) return false;
    return (depths & (1UL << 15)) != 0;
}

UInt32 hires_main(void)
{
    EventType e;
    UInt16 err;
    Boolean changed;
    FormType *f = FrmInitForm(1100);
    FrmSetActiveForm(f);
    FrmDrawForm(f);
    lib_open();
    gfx_init();
    draw_all();
    do {
        EvtGetEvent(&e, SysTicksPerSecond() / 15);
        if (SysHandleEvent(&e) || MenuHandleEvent(NULL, &e, &err)) continue;
        if (e.eType == penDownEvent && e.screenY < 160) {
            tap((short)(e.screenX * 2), (short)(e.screenY * 2));
        } else if (e.eType == nilEvent && page == P_LINK) {
            ++anim;
            pulse = (UInt8)((anim & 15) < 8 ? (anim & 7) * 2 : 14 - (anim & 7) * 2);
            changed = !(anim & 15) && poll_status();
            gfx_begin(0);
            route(link_up());
            if (!(anim & 15)) link_status();
            if (changed) notice(252);
            gfx_end();
        } else if (e.eType == frmUpdateEvent)
            draw_all();
    } while (e.eType != appStopEvent);
    gfx_done();
    lib_close();
    FrmCloseAllForms();
    return 0;
}
