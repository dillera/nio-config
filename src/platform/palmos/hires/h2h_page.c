/* NIO Config's H2H page, part of hires.c (included there, so it shares its drawing, sound and NioLib state) in a
 * build with H2H_DIR (makefiles/palmos.mk): the head-to-head speed test's suite (head-to-head-test's palm/suite.c,
 * linked in), run through NioLib's saved transport against the shared h2h server, its results then sent there under
 * the tester's name, as the H2H apps' Send does. NAME sets that name (a Palm with no desktop HotSync has no HotSync
 * name): it is saved in NIO Config's preferences, and starts as the HotSync name where there is one.
 *
 * Before each run the page sets the Palm's clock from NIO: the device's clock (fn_clock_get), or, where the device
 * has none (NioLib's Network transport: the NIO on the Palm), the RFC 868 time from time.nist.gov over a NIO TCP
 * connection. The Palm keeps local time: its time zone and daylight saving setting are added. */
#include <DLServer.h>
#include "h2h.h"

#define H2H_TARGET "http://fujinet.online:8199/h2h"
#define H2H_SHOWN "fujinet.online:8199/h2h"
#define H2H_LOG 12
#define H2H_LOG_CHARS 52
#define H2H_BODY_Y (PAGE_Y + 62)
#define H2H_BODY_H 170
#define H2H_NOTE_Y (SCREEN_H - TAB_H - 16)

#define UNIX_TO_PALM 2082844800UL   /* seconds from 1904-01-01 (the Palm's epoch) to 1970-01-01 */
#define RFC868_TO_UNIX 2208988800UL /* seconds from 1900-01-01 to 1970-01-01 */
#define SANE_UNIX 1600000000UL      /* 2020: anything earlier is a clock that was never set */

#define H2H_PREF_ID 0x4832          /* NIO Config's preference 'H2': the tester's name */
#define H2H_PREF_VERSION 1
#define H2H_NAME_ALERT 1200         /* hires.rcp: the name dialog */
#define NIOCONFIG_CREATOR 'NiC1'

static Settings h2h_set;
static Char h2h_name[sizeof(h2h_set.name)];
static Boolean h2h_named;           /* h2h_name has been read */
static Char h2h_log[H2H_LOG][H2H_LOG_CHARS];
static UInt8 h2h_lines;
static Boolean h2h_done, h2h_sent;     /* a run's results are on screen; they reached the server */
static Char h2h_clock[80], h2h_note[64];

/* ---------------------------------------------------------------- what the suite needs from its app */

const Char *h2h_host_stack(void)
{
    switch (cfg.transport) {
    case nioTransportNetwork: return "niolib-network";
    case nioTransportSerial: return "niolib-serial";
    case nioTransportUSB: return "niolib-usb";
    default: return "niolib-springboard-ram";
    }
}

const Char *h2h_host_title(void)
{
    switch (cfg.transport) {
    case nioTransportNetwork: return "NioLib Network, NIO on the Palm";
    case nioTransportSerial: return "NioLib serial";
    case nioTransportUSB: return "NioLib USB";
    default: return "NioLib SB, app in RAM";
    }
}

static void h2h_body(void);

void ui_progress(const Char *line)
{
    UInt8 i;
    if (h2h_lines == H2H_LOG) {
        for (i = 1; i < H2H_LOG; ++i) StrCopy(h2h_log[i - 1], h2h_log[i]);
        --h2h_lines;
    }
    StrNCopy(h2h_log[h2h_lines], line, H2H_LOG_CHARS - 1);
    h2h_log[h2h_lines][H2H_LOG_CHARS - 1] = 0;
    ++h2h_lines;
    EvtResetAutoOffTimer();
    gfx_begin(0); h2h_body(); gfx_end();
}

/* A tap or a key stops the run; an app switch stops it and then happens. */
Boolean ui_stop_requested(void)
{
    EventType e;
    if (!EvtSysEventAvail(true)) return false;
    EvtGetEvent(&e, 0);
    if (e.eType == penDownEvent || e.eType == keyDownEvent) return true;
    if (e.eType == appStopEvent) { EvtAddEventToQueue(&e); return true; }
    return false;
}

/* ---------------------------------------------------------------- the Palm's clock from NIO */

/* Unix time from the NIO: its clock device, else RFC 868 from time.nist.gov over TCP (four bytes, seconds since
 * 1900, then the server closes). A client session must be open. */
static Boolean nio_time(UInt32 *unix, const Char **from)
{
    FN_TIME_T t = 0;
    fn_handle_t h;
    UInt8 b[4], flags = 0, r;
    uint16_t got = 0, have = 0;
    UInt32 deadline, v;

    if (fn_clock_get(&t) == FN_OK && t > SANE_UNIX && t < 0xFFFFFFFFUL - UNIX_TO_PALM) {
        *unix = (UInt32)t;
        *from = "NIO CLOCK";
        return true;
    }
    if (fn_tcp_open(&h, "time.nist.gov", 37) != FN_OK) return false;
    deadline = TimGetTicks() + 10 * SysTicksPerSecond();
    while (have < 4 && TimGetTicks() < deadline) {
        r = fn_read(h, have, b + have, (uint16_t)(4 - have), &got, &flags);
        if (r == FN_OK) {
            have += got;
            if (flags & FN_READ_EOF) break;
        } else if (r != FN_ERR_NOT_READY)
            break;
    }
    fn_close(h);
    if (have < 4) return false;
    v = ((UInt32)b[0] << 24) | ((UInt32)b[1] << 16) | ((UInt32)b[2] << 8) | b[3];
    if (v < RFC868_TO_UNIX + SANE_UNIX) return false;
    *unix = v - RFC868_TO_UNIX;
    *from = "NIST VIA NIO";
    return true;
}

static void h2h_set_clock(void)
{
    UInt32 unix, secs;
    Int32 zone;
    const Char *from = "";
    DateTimeType dt;
    Char d[longDateStrLength], tm[timeStringLength];
    Boolean ok;

    if (fn_init() != FN_OK) { StrCopy(h2h_clock, "CLOCK NOT SET: NIOLIB WOULD NOT START"); return; }
    ok = nio_time(&unix, &from);
    fn_shutdown();
    if (!ok) { StrCopy(h2h_clock, "CLOCK NOT SET: NO TIME FROM NIO"); return; }
    zone = (Int16)PrefGetPreference(prefTimeZone) + (Int16)PrefGetPreference(prefDaylightSavingAdjustment);
    secs = unix + UNIX_TO_PALM + zone * 60;
    TimSetSeconds(secs);
    TimSecondsToDateTime(secs, &dt);
    DateToAscii((UInt8)dt.month, (UInt8)dt.day, (UInt16)dt.year, (DateFormatType)PrefGetPreference(prefDateFormat), d);
    TimeToAscii((UInt8)dt.hour, (UInt8)dt.minute, (TimeFormatType)PrefGetPreference(prefTimeFormat), tm);
    StrPrintF(h2h_clock, "CLOCK SET %s %s (%s)", d, tm, from);
}

/* ---------------------------------------------------------------- the tester's name */

static void h2h_load_name(void)
{
    UInt16 size = sizeof(h2h_name);
    Char user[dlkUserNameBufSize];
    if (h2h_named) return;
    h2h_named = true;
    if (PrefGetAppPreferences(NIOCONFIG_CREATOR, H2H_PREF_ID, h2h_name, &size, true) == H2H_PREF_VERSION && size) {
        h2h_name[sizeof(h2h_name) - 1] = 0;
        return;
    }
    h2h_name[0] = 0;
    user[0] = 0;
    if (DlkGetSyncInfo(NULL, NULL, NULL, user, NULL, NULL) == errNone && user[0]) {
        StrNCopy(h2h_name, user, sizeof(h2h_name) - 1);
        h2h_name[sizeof(h2h_name) - 1] = 0;
    }
}

/* The dialog's field starts with the name, selected, so typing replaces it. */
static Boolean h2h_name_field(Int16 button, Char *attempt)
{
    FormType *f;
    FieldType *fld;
    UInt16 i, n, len;
    (void)attempt;
    if (button != frmResponseCreate) return true;
    f = FrmGetActiveForm();
    n = f ? FrmGetNumberOfObjects(f) : 0;
    for (i = 0; i < n; ++i)
        if (FrmGetObjectType(f, i) == frmFieldObj) {
            fld = (FieldType *)FrmGetObjectPtr(f, i);
            len = StrLen(h2h_name);
            FldSetMaxChars(fld, sizeof(h2h_name) - 1);
            if (len) {
                FldInsert(fld, h2h_name, len);
                FldSetSelection(fld, 0, len);
            }
            break;
        }
    return true;
}

static void draw_all(void);

/* NAME: the standard dialog over the colour screen, then the screen again. True when a name was saved. */
static Boolean h2h_ask_name(void)
{
    Char buf[sizeof(h2h_name)];
    Char *p, *e;
    UInt16 button;
    buf[0] = 0;
    button = FrmCustomResponseAlert(H2H_NAME_ALERT, "", "", "", buf, sizeof(buf), h2h_name_field);
    draw_all();
    if (button != 0) return false;
    for (p = buf; *p == ' '; ++p) {}
    for (e = p + StrLen(p); e > p && e[-1] == ' '; --e) {}
    *e = 0;
    if (!*p) return false;
    StrCopy(h2h_name, p);
    PrefSetAppPreferences(NIOCONFIG_CREATOR, H2H_PREF_ID, H2H_PREF_VERSION, h2h_name, StrLen(h2h_name) + 1, true);
    return true;
}

/* ---------------------------------------------------------------- drawing */

static void h2h_body(void)
{
    short y = H2H_BODY_Y, x, ry;
    UInt8 i;
    Char ms[12], rate[12];
    const Row *r;
    rgb_t c;

    card(8, y, SCREEN_W - 16, H2H_BODY_H, h2h_sent ? mix(C_GREEN, C_BG0, 6) : C_EDGE);
    if (!h2h_done) {
        if (!h2h_lines) {
            say(18, y + 8, "RUN sets the Palm's clock from NIO, runs the", F_TINY, C_DIM);
            say(18, y + 21, "head-to-head speed test through NioLib, then", F_TINY, C_DIM);
            say(18, y + 34, "sends the results to " H2H_SHOWN, F_TINY, C_DIM);
            say(18, y + 47, "under your NAME.", F_TINY, C_DIM);
            say(18, y + 69, "A full run takes several minutes.", F_TINY, C_DIM);
            say(18, y + 82, "Tap the screen to stop it early.", F_TINY, C_DIM);
            return;
        }
        for (i = 0; i < h2h_lines; ++i)
            say(18, y + 6 + i * 13, h2h_log[i], F_TINY, i + 1 == h2h_lines ? C_WHITE : C_DIM);
        return;
    }
    for (i = 0; i < 2; ++i) {
        x = 18 + i * 148;
        say(x, y + 6, "TEST", F_TINY, C_DIM);
        say_r(x + 92, y + 6, "MS", F_TINY, C_DIM);
        say_r(x + 136, y + 6, "KB/S", F_TINY, C_DIM);
    }
    for (i = 0; i < gResults.count && i < 22; ++i) {
        r = &gResults.rows[i];
        x = 18 + (i / 11) * 148;
        ry = y + 20 + (i % 11) * 13;
        c = r->fails ? C_RED : C_TEXT;
        say(x, ry, r->test, F_TINY, c);
        if (r->fails >= r->reps) { say_r(x + 136, ry, "FAILED", F_TINY, C_RED); continue; }
        /* Plain numbers: the headings carry the units, and units would run the columns together. */
        StrPrintF(ms, "%lu", (r->usTotal + 500) / 1000);
        say_r(x + 92, ry, ms, F_TINY, c);
        if (r->bytes) {
            Char *k;
            fmt_rate(rate, r->bytes, r->usTotal);
            if ((k = StrChr(rate, 'K')) != NULL) *k = 0;
            say_r(x + 136, ry, rate, F_TINY, r->fails ? C_RED : C_GREEN);
        }
    }
    if (!gResults.count) say(18, y + 22, "No results.", F_TINY, C_DIM);
}

static Boolean h2h_can_send(void) { return h2h_done && !h2h_sent && gResults.count && !gResults.page; }

static void draw_h2h(void)
{
    Char t[64];
    h2h_load_name();
    gfx_rect(0, PAGE_Y, SCREEN_W, PAGE_H, C_BG0);
    button(8, PAGE_Y + 2, 96, 26, "NAME", C_VIOLET, false);
    if (h2h_can_send()) button(SCREEN_W - 204, PAGE_Y + 2, 96, 26, "SEND", C_GREEN, false);
    button(SCREEN_W - 104, PAGE_Y + 2, 96, 26, "RUN", C_CYAN, true);
    if (h2h_name[0]) {
        StrPrintF(t, "AS %s  VIA %s", h2h_name, h2h_host_stack());
        say(8, PAGE_Y + 33, t, F_TINY, C_TEXT);
    } else
        say(8, PAGE_Y + 33, "NO NAME YET: TAP NAME", F_TINY, C_AMBER);
    say(8, PAGE_Y + 46, h2h_clock[0] ? h2h_clock : "TO " H2H_SHOWN, F_TINY,
        h2h_clock[0] ? (StrNCompare(h2h_clock, "CLOCK SET", 9) ? C_AMBER : C_GREEN) : C_DIM);
    h2h_body();
    notice(H2H_NOTE_Y);
}

static void h2h_say(const Char *text, rgb_t c)
{
    StrNCopy(h2h_note, text, sizeof(h2h_note) - 1);
    h2h_note[sizeof(h2h_note) - 1] = 0;
    note = h2h_note;
    note_c = c;
    gfx_begin(0); notice(H2H_NOTE_Y); gfx_end();
}

/* ---------------------------------------------------------------- run and send */

static void h2h_send(void)
{
    UInt16 sent = 0;
    Err err;
    Char t[128];
    if (!h2h_name[0] && !h2h_ask_name()) { h2h_say("RESULTS NEED A NAME: TAP NAME", C_RED); play(s_error); return; }
    StrCopy(h2h_set.name, h2h_name);
    h2h_say("SENDING TO " H2H_SHOWN "...", C_AMBER);
    err = suite_upload(&h2h_set, &sent);
    if (!err) {
        h2h_sent = true;
        StrPrintF(t, "SENT: SEE %s/report", H2H_SHOWN);
        h2h_say(t, C_GREEN);
        play(s_up);
    } else {
        StrPrintF(t, "SENT %u, THEN %s: TAP SEND", sent, link_err_text(err));
        h2h_say(t, C_RED);
        play(s_error);
    }
}

static void h2h_run(void)
{
    UInt8 i, failed = 0;
    if (!open_) { h2h_say("NIOLIB IS NOT INSTALLED", C_RED); play(s_error); return; }
    h2h_load_name();
    if (!h2h_name[0] && !h2h_ask_name()) { h2h_say("RESULTS NEED A NAME: TAP NAME", C_RED); play(s_error); return; }
    MemSet(&h2h_set, sizeof(h2h_set), 0);
    StrCopy(h2h_set.name, h2h_name);
    StrCopy(h2h_set.target, H2H_TARGET);
    h2h_set.reps = 3;
    h2h_set.big = true;
    h2h_set.sweep = true;
    h2h_done = h2h_sent = false;
    h2h_lines = 0;
    h2h_clock[0] = 0;
    play(s_click);
    note = "SETTING THE CLOCK FROM NIO..."; note_c = C_AMBER;
    gfx_begin(1); draw_h2h(); gfx_end();
    h2h_set_clock();
    note = "RUNNING: TAP TO STOP"; note_c = C_AMBER;
    gfx_begin(1); draw_h2h(); gfx_end();
    suite_run(&h2h_set, false);
    h2h_done = true;
    for (i = 0; i < gResults.count; ++i) if (gResults.rows[i].fails) ++failed;
    gfx_begin(1); draw_h2h(); gfx_end();
    if (gResults.startErr) {
        Char t[128];
        StrPrintF(t, "COULD NOT START: %s", link_err_text(gResults.startErr));
        h2h_say(t, C_RED);
        play(s_error);
        return;
    }
    if (gResults.stopped) { h2h_say("STOPPED: NOT SENT (TAP SEND TO SEND IT)", C_AMBER); play(s_partly); }
    else {
        play(failed ? s_partly : s_done);
        h2h_send();
    }
    gfx_begin(0); draw_h2h(); gfx_end();
}

static void h2h_tap(short x, short y)
{
    if (y >= PAGE_Y + 30) return;
    if (x < 8 + 96) {
        play(s_click);
        if (h2h_ask_name()) { h2h_say("NAME SAVED", C_GREEN); play(s_saved); }
    } else if (x >= SCREEN_W - 104)
        h2h_run();
    else if (x >= SCREEN_W - 204 && h2h_can_send()) {
        play(s_click);
        h2h_send();
        gfx_begin(0); draw_h2h(); gfx_end();
    }
}
