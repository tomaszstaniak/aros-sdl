/* Checks for the AROS video backend of SDL3 (contrib SDL3-3.4.12-aros.diff).
 *
 *   sdl3-contrib-check pointer   two clicks while the program is busy land where they were made
 *   sdl3-contrib-check size      inner size after WA_AutoAdjust; SDL_RestoreWindow keeps the size
 *   sdl3-contrib-check text abc  typed keys arrive as SDL_EVENT_TEXT_INPUT
 *   sdl3-contrib-check iconify   iconify gadget -> AppIcon -> double-click restores the window
 *   sdl3-contrib-check drop      a file icon dropped on the window arrives with a UTF-8 path
 *   sdl3-contrib-check charset   window title and FTXT clipboard text in the system character set
 *
 * pointer, text, iconify and drop need input from outside (a person or the
 * host's QEMU input): the window shows what to do by its colour. Every mode
 * prints one line per check and "result: pass" or "result: fail", waits a
 * bounded time, and removes what it created (drop: RAM:sdl3drop, charset:
 * restores ENV:CHARSET). Run it again in the same boot as often as needed.
 */
#include <SDL3/SDL.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <workbench/icon.h>
#include <workbench/workbench.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("%s: %s%s%s\n", what, ok ? "ok" : "FAIL", detail && *detail ? " - " : "", detail ? detail : "");
    if (!ok)
        failures++;
}

static void hex(char *out, size_t size, const char *s)
{
    size_t o = 0;
    out[0] = '\0';
    for (; *s && o + 4 < size; s++)
        o += (size_t)snprintf(out + o, size - o, "%02x", (unsigned char)*s);
}

/* The Intuition window whose title is title (SDL keeps it as given for ASCII). */
static struct Window *find_window(const char *title)
{
    struct Window *found = NULL;
    ULONG lock = LockIBase(0);
    for (struct Screen *s = IntuitionBase->FirstScreen; s && !found; s = s->NextScreen)
        for (struct Window *w = s->FirstWindow; w && !found; w = w->NextWindow)
            if (w->Title && !strcmp((const char *)w->Title, title))
                found = w;
    UnlockIBase(lock);
    return found;
}

static void inner_size(struct Window *w, int *iw, int *ih)
{
    *iw = w->Width - w->BorderLeft - w->BorderRight;
    *ih = w->Height - w->BorderTop - w->BorderBottom;
}

static void fill(SDL_Renderer *r, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderClear(r);
}

static void rect(SDL_Renderer *r, SDL_FRect f, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderFillRect(r, &f);
}

typedef int (*event_fn)(const SDL_Event *e, void *user);   /* nonzero: stop */

/* Handle events for at most ms milliseconds. Returns 1 when fn asked to stop,
   -1 on a close request, 0 on timeout. */
static int pump(Uint32 ms, event_fn fn, void *user)
{
    Uint64 end = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                return -1;
            if (fn && fn(&e, user))
                return 1;
        }
        SDL_Delay(10);
    }
    return 0;
}

/* --- pointer ------------------------------------------------------------ */

static const SDL_FRect target_a = { 40, 40, 60, 60 }, target_b = { 280, 180, 60, 60 };

struct clicks { int n; float x[8], y[8]; };

static int on_click(const SDL_Event *e, void *user)
{
    struct clicks *c = user;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN && c->n < 8) {
        c->x[c->n] = e->button.x;
        c->y[c->n] = e->button.y;
        printf("button down at %.0f,%.0f\n", e->button.x, e->button.y);
        c->n++;
    }
    return 0;
}

static int inside(SDL_FRect f, float x, float y)
{
    return x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h;
}

static void mode_pointer(SDL_Window *win, SDL_Renderer *r)
{
    struct clicks c = { 0 };
    char buf[96];
    /* Grey: get ready. Green: the program is busy for 20 seconds and handles
       no events; click the red square, then the blue one, during that time. */
    fill(r, 128, 128, 128);
    rect(r, target_a, 220, 30, 30);
    rect(r, target_b, 30, 30, 220);
    SDL_RenderPresent(r);
    pump(4000, NULL, NULL);
    fill(r, 30, 140, 30);
    rect(r, target_a, 220, 30, 30);
    rect(r, target_b, 30, 30, 220);
    SDL_RenderPresent(r);
    pump(200, NULL, NULL);
    puts("busy for 20 s: click red, then blue");
    fflush(stdout);
    SDL_Delay(20000);
    pump(3000, on_click, &c);
    (void)win;
    snprintf(buf, sizeof buf, "%d button presses", c.n);
    check(c.n >= 2, "clicks", buf);
    if (c.n >= 2) {
        snprintf(buf, sizeof buf, "first at %.0f,%.0f, red square %.0f,%.0f %.0fx%.0f",
                 c.x[0], c.y[0], target_a.x, target_a.y, target_a.w, target_a.h);
        check(inside(target_a, c.x[0], c.y[0]), "first click", buf);
        snprintf(buf, sizeof buf, "second at %.0f,%.0f, blue square %.0f,%.0f %.0fx%.0f",
                 c.x[1], c.y[1], target_b.x, target_b.y, target_b.w, target_b.h);
        check(inside(target_b, c.x[1], c.y[1]), "second click", buf);
    }
}

/* --- size --------------------------------------------------------------- */

static void compare_size(SDL_Window *win, const char *title, const char *what)
{
    char buf[128];
    int sw, sh, iw = -1, ih = -1;
    struct Window *w = find_window(title);
    SDL_GetWindowSize(win, &sw, &sh);
    if (w)
        inner_size(w, &iw, &ih);
    snprintf(buf, sizeof buf, "SDL %dx%d, Intuition inner %dx%d", sw, sh, iw, ih);
    check(w && sw == iw && sh == ih, what, buf);
}

static void mode_size(void)
{
    char buf[128];
    /* SDL_RestoreWindow on a window that is neither maximized nor minimized
       must leave its size alone. */
    SDL_Window *win = SDL_CreateWindow("sdl3check restore", 320, 200, SDL_WINDOW_RESIZABLE);
    check(win != NULL, "small window", win ? "" : SDL_GetError());
    if (win) {
        int iw0 = -1, ih0 = -1, iw1 = -1, ih1 = -1;
        struct Window *w;
        pump(1500, NULL, NULL);
        w = find_window("sdl3check restore");
        if (w)
            inner_size(w, &iw0, &ih0);
        compare_size(win, "sdl3check restore", "size after open");
        SDL_RestoreWindow(win);
        pump(1500, NULL, NULL);
        if (w)
            inner_size(w, &iw1, &ih1);
        snprintf(buf, sizeof buf, "inner %dx%d before SDL_RestoreWindow, %dx%d after", iw0, ih0, iw1, ih1);
        check(w && iw0 == iw1 && ih0 == ih1, "restore keeps size", buf);
        compare_size(win, "sdl3check restore", "size after restore");
        SDL_DestroyWindow(win);
    }

    /* A window as large as the screen cannot keep that inner size with its
       borders: Intuition shrinks it (WA_AutoAdjust). */
    SDL_Rect b = { 0, 0, 0, 0 };
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &b);
    printf("display %dx%d\n", b.w, b.h);
    win = SDL_CreateWindow("sdl3check full", b.w, b.h, SDL_WINDOW_RESIZABLE);
    check(win != NULL, "screen-size window", win ? "" : SDL_GetError());
    if (win) {
        pump(2000, NULL, NULL);
        compare_size(win, "sdl3check full", "screen-size window size");
        SDL_DestroyWindow(win);
    }
}

/* --- text --------------------------------------------------------------- */

struct text { char got[64]; int keys; size_t want; };

static int on_text(const SDL_Event *e, void *user)
{
    struct text *t = user;
    if (e->type == SDL_EVENT_KEY_DOWN)
        t->keys++;
    if (e->type == SDL_EVENT_TEXT_INPUT) {
        printf("text \"%s\"\n", e->text.text);
        SDL_strlcat(t->got, e->text.text, sizeof t->got);
    }
    return strlen(t->got) >= t->want;
}

static void mode_text(SDL_Window *win, SDL_Renderer *r, const char *want)
{
    struct text t = { "", 0, strlen(want) };
    char buf[128];
    /* Yellow: type the expected text into this window. */
    fill(r, 230, 200, 30);
    SDL_RenderPresent(r);
    SDL_StartTextInput(win);
    printf("type \"%s\" within 40 s\n", want);
    fflush(stdout);
    pump(40000, on_text, &t);
    SDL_StopTextInput(win);
    snprintf(buf, sizeof buf, "%d key presses, text \"%s\", expected \"%s\"", t.keys, t.got, want);
    check(t.keys > 0, "key events", buf);
    check(!strcmp(t.got, want), "text input", buf);
}

/* --- iconify ------------------------------------------------------------ */

struct wstate { int minimized, restored; };

static int on_window(const SDL_Event *e, void *user)
{
    struct wstate *s = user;
    if (e->type >= SDL_EVENT_WINDOW_FIRST && e->type <= SDL_EVENT_WINDOW_LAST)
        printf("window event 0x%x\n", (unsigned)e->type);
    if (e->type == SDL_EVENT_WINDOW_MINIMIZED) {
        s->minimized++;
        puts("minimized: double-click the AppIcon");
        fflush(stdout);
    }
    if (e->type == SDL_EVENT_WINDOW_RESTORED && s->minimized)
        s->restored++;
    return s->restored > 0;
}

static void mode_iconify(SDL_Window *win, SDL_Renderer *r)
{
    struct wstate s = { 0, 0 };
    /* Magenta: click the iconify gadget, then double-click the AppIcon. */
    fill(r, 200, 40, 200);
    SDL_RenderPresent(r);
    puts("click the iconify gadget within 60 s");
    fflush(stdout);
    pump(60000, on_window, &s);
    check(s.minimized > 0, "minimized", s.minimized ? "SDL_EVENT_WINDOW_MINIMIZED" : "no SDL_EVENT_WINDOW_MINIMIZED");
    check(s.restored > 0, "restored", s.restored ? "SDL_EVENT_WINDOW_RESTORED" : "no SDL_EVENT_WINDOW_RESTORED");
    if (s.restored) {
        /* The window is back: show it for a screenshot. */
        fill(r, 200, 40, 200);
        SDL_RenderPresent(r);
        pump(3000, NULL, NULL);
    }
    (void)win;
}

/* --- drop --------------------------------------------------------------- */

#define DROP_DIR "RAM:sdl3drop"
#define DROP_NAME "caf\xe9.txt"            /* ISO-8859-1 and -2: e acute is 0xE9 */
#define DROP_NAME_UTF8 "caf\xc3\xa9.txt"

static void drop_cleanup(void)
{
    DeleteFile((CONST_STRPTR)DROP_DIR "/" DROP_NAME ".info");
    DeleteFile((CONST_STRPTR)DROP_DIR "/" DROP_NAME);
    if (!DeleteFile((CONST_STRPTR)DROP_DIR) && IoErr() != ERROR_OBJECT_NOT_FOUND)
        printf("note: " DROP_DIR " not removed (error %ld; close its Wanderer window)\n", (long)IoErr());
}

static int drop_prepare(void)
{
    BPTR lock = CreateDir((CONST_STRPTR)DROP_DIR);
    if (lock)
        UnLock(lock);
    BPTR fh = Open((CONST_STRPTR)DROP_DIR "/" DROP_NAME, MODE_NEWFILE);
    if (!fh)
        return 0;
    Write(fh, "sdl3 drop test\n", 15);
    Close(fh);
    struct DiskObject *dob = GetDefDiskObject(WBPROJECT);
    int ok = dob && PutDiskObject((CONST_STRPTR)DROP_DIR "/" DROP_NAME, dob);
    if (dob)
        FreeDiskObject(dob);
    return ok;
}

struct drop { char path[512]; int n; };

static int on_drop(const SDL_Event *e, void *user)
{
    struct drop *d = user;
    if (e->type == SDL_EVENT_DROP_FILE && e->drop.data) {
        char h[256];
        hex(h, sizeof h, e->drop.data);
        printf("drop file \"%s\" (bytes %s)\n", e->drop.data, h);
        SDL_strlcpy(d->path, e->drop.data, sizeof d->path);
        d->n++;
    }
    return d->n > 0;
}

static void mode_drop(SDL_Window *win, SDL_Renderer *r)
{
    struct drop d = { "", 0 };
    char buf[600];
    check(drop_prepare(), "prepare", DROP_DIR "/" DROP_NAME " with an icon");
    /* Cyan: open RAM:sdl3drop in Wanderer and drag the icon onto this window. */
    fill(r, 30, 200, 200);
    SDL_RenderPresent(r);
    puts("drop " DROP_DIR "/" DROP_NAME " on the window within 90 s");
    fflush(stdout);
    pump(90000, on_drop, &d);
    check(d.n > 0, "drop event", d.n ? "SDL_EVENT_DROP_FILE" : "none");
    if (d.n) {
        size_t pl = strlen(d.path), nl = strlen(DROP_NAME_UTF8);
        snprintf(buf, sizeof buf, "path \"%s\", expected to end in UTF-8 \"%s\"", d.path, DROP_NAME_UTF8);
        check(pl >= nl && !strcmp(d.path + pl - nl, DROP_NAME_UTF8), "drop path UTF-8", buf);
        /* Green: event received; the window stays for a while so that
           Workbench can be checked. */
        fill(r, 30, 140, 30);
        SDL_RenderPresent(r);
        pump(20000, NULL, NULL);
    }
    (void)win;
}

/* --- charset ------------------------------------------------------------ */

static const char utf8_text[] = "za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87";   /* zażółć */
static const char latin2_text[] = "za\xbf\xf3\xb3\xe6";

static struct IOClipReq *clip_open(void)
{
    struct MsgPort *mp = CreateMsgPort();
    struct IOClipReq *io = mp ? (struct IOClipReq *)CreateIORequest(mp, sizeof(struct IOClipReq)) : NULL;
    if (io && !OpenDevice((CONST_STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest *)io, 0))
        return io;
    if (io)
        DeleteIORequest((struct IORequest *)io);
    if (mp)
        DeleteMsgPort(mp);
    return NULL;
}

static void clip_close(struct IOClipReq *io)
{
    struct MsgPort *mp = io->io_Message.mn_ReplyPort;
    CloseDevice((struct IORequest *)io);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(mp);
}

static void put_be32(UBYTE *p, ULONG v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static ULONG get_be32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

/* Native FTXT with a CHRS chunk only, as programs without UTF-8 write it. */
static int clip_write_chrs(const char *bytes)
{
    struct IOClipReq *io = clip_open();
    UBYTE buf[64];
    ULONG n = (ULONG)strlen(bytes), pad = n & 1, total = 4 + 8 + n + pad;
    if (!io || 20 + n + pad > sizeof buf)
        return 0;
    memcpy(buf, "FORM", 4);
    put_be32(buf + 4, total);
    memcpy(buf + 8, "FTXT", 4);
    memcpy(buf + 12, "CHRS", 4);
    put_be32(buf + 16, n);
    memcpy(buf + 20, bytes, n);
    if (pad)
        buf[20 + n] = 0;
    io->io_Offset = 0;
    io->io_Error = 0;
    io->io_ClipID = 0;
    io->io_Command = CMD_WRITE;
    io->io_Data = (STRPTR)buf;
    io->io_Length = 20 + n + pad;
    DoIO((struct IORequest *)io);
    int ok = io->io_Error == 0;
    io->io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)io);
    clip_close(io);
    return ok;
}

/* The first bytes on the clipboard, in hex, for the report. */
static char clip_head[80];

/* The CHRS chunk of the FTXT on the clipboard, as raw bytes. */
static int clip_read_chrs(char *out, size_t size)
{
    struct IOClipReq *io = clip_open();
    static UBYTE buf[4096];
    ULONG got = 0;
    int found = 0;
    if (!io)
        return 0;
    io->io_Offset = 0;
    io->io_ClipID = 0;
    for (;;) {
        io->io_Command = CMD_READ;
        io->io_Data = (STRPTR)(buf + got);
        io->io_Length = got < sizeof buf ? sizeof buf - got : 0;
        if (!io->io_Length) {
            static UBYTE sink[256];
            io->io_Data = (STRPTR)sink;
            io->io_Length = sizeof sink;
        }
        DoIO((struct IORequest *)io);
        if (io->io_Actual == 0)
            break;   /* reading past the end ends the read */
        if (io->io_Data != (STRPTR)buf + got)
            continue;
        got += io->io_Actual;
    }
    clip_close(io);
    clip_head[0] = '\0';
    for (ULONG i = 0; i < got && i < 24; i++)
        snprintf(clip_head + 2 * i, sizeof clip_head - 2 * i, "%02x", buf[i]);
    if (got >= 12 && !memcmp(buf, "FORM", 4) && !memcmp(buf + 8, "FTXT", 4)) {
        ULONG p = 12;
        while (p + 8 <= got) {
            ULONG len = get_be32(buf + p + 4);
            if (!memcmp(buf + p, "CHRS", 4) && p + 8 + len <= got) {
                size_t n = len < size - 1 ? len : size - 1;
                memcpy(out, buf + p + 8, n);
                out[n] = '\0';
                found = 1;
                break;
            }
            p += 8 + len + (len & 1);
        }
    }
    return found;
}

static void mode_charset(SDL_Window *win)
{
    char buf[256], h1[64], h2[64], raw[128];
    struct Window *w = find_window("sdl3check");

    /* 1. A CHRS-only clipboard text in the system character set, read by SDL. */
    check(clip_write_chrs(latin2_text), "clipboard write (native CHRS)", "");
    char *got = SDL_GetClipboardText();
    hex(h1, sizeof h1, got ? got : "");
    hex(h2, sizeof h2, utf8_text);
    snprintf(buf, sizeof buf, "SDL_GetClipboardText bytes %s, expected %s", h1, h2);
    check(got && !strcmp(got, utf8_text), "clipboard CHRS to UTF-8", buf);
    SDL_free(got);

    /* 2. SDL's clipboard text, as a program reading CHRS sees it. */
    SDL_SetClipboardText(utf8_text);
    raw[0] = '\0';
    int ok = clip_read_chrs(raw, sizeof raw);
    hex(h1, sizeof h1, raw);
    hex(h2, sizeof h2, latin2_text);
    snprintf(buf, sizeof buf, "CHRS bytes %s, expected %s; clipboard starts %s", ok ? h1 : "(none)", h2, clip_head);
    check(ok && !strcmp(raw, latin2_text), "clipboard UTF-8 to CHRS", buf);

    /* 3. The window title. */
    check(w != NULL, "window found", "");
    if (w) {
        SDL_SetWindowTitle(win, utf8_text);
        pump(500, NULL, NULL);
        hex(h1, sizeof h1, w->Title ? (const char *)w->Title : "");
        snprintf(buf, sizeof buf, "Intuition title bytes %s, expected %s", h1, h2);
        check(w->Title && !strcmp((const char *)w->Title, latin2_text), "window title", buf);
    }
    clip_write_chrs("");
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    char saved_charset[64];
    int had_charset = 0;

    if (!strcmp(mode, "charset")) {
        /* The system character set for this check, restored at the end. */
        had_charset = GetVar((CONST_STRPTR)"CHARSET", (STRPTR)saved_charset, sizeof saved_charset, GVF_GLOBAL_ONLY) > 0;
        SetVar((CONST_STRPTR)"CHARSET", (CONST_STRPTR)"ISO-8859-2", -1, GVF_GLOBAL_ONLY);
        puts("ENV:CHARSET ISO-8859-2 for this check");
    }
    printf("SDL %d.%d.%d, mode %s\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION, mode);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        check(0, "init", SDL_GetError());
    } else if (!strcmp(mode, "size")) {
        mode_size();
    } else {
        SDL_Window *win = SDL_CreateWindow("sdl3check", 400, 300, 0);
        SDL_Renderer *r = win ? SDL_CreateRenderer(win, "software") : NULL;
        check(win && r, "window", win && r ? "" : SDL_GetError());
        if (win && r) {
            pump(500, NULL, NULL);
            if (!strcmp(mode, "pointer"))
                mode_pointer(win, r);
            else if (!strcmp(mode, "text"))
                mode_text(win, r, argc > 2 ? argv[2] : "aB1");
            else if (!strcmp(mode, "iconify"))
                mode_iconify(win, r);
            else if (!strcmp(mode, "drop"))
                mode_drop(win, r);
            else if (!strcmp(mode, "charset"))
                mode_charset(win);
            else
                check(0, "mode", "pointer, size, text, iconify, drop or charset");
        }
        if (r)
            SDL_DestroyRenderer(r);
        if (win)
            SDL_DestroyWindow(win);
    }
    SDL_Quit();
    if (!strcmp(mode, "drop"))
        drop_cleanup();
    if (!strcmp(mode, "charset")) {
        if (had_charset)
            SetVar((CONST_STRPTR)"CHARSET", (CONST_STRPTR)saved_charset, -1, GVF_GLOBAL_ONLY);
        else
            DeleteVar((CONST_STRPTR)"CHARSET", GVF_GLOBAL_ONLY);
    }
    puts(failures ? "result: fail" : "result: pass");
    return failures ? 10 : 0;
}
