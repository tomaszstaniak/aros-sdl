/* Self-test of the AROS SDL kit (SDL3 part).
 *
 *   aros-sdl3-selftest [software|opengl|all] [seconds] [width height]
 *
 * Checks SDL_Init(VIDEO), a window, each requested renderer (draws and reads
 * back a frame), SDL3_image (encodes and decodes a PNG in memory) and SDL's
 * iconv (UTF-8 <-> ISO-8859-1). With [seconds] the window stays open so the
 * iconify and zoom gadgets can be tried by hand; window events are printed.
 * Prints one line per check and "result: pass" or "result: fail".
 */
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("%s: %s%s%s\n", what, ok ? "ok" : "FAIL", detail && *detail ? " - " : "", detail ? detail : "");
    if (!ok)
        failures++;
}

static void try_renderer(SDL_Window *win, const char *name)
{
    char buf[160];
    SDL_Renderer *r = SDL_CreateRenderer(win, name);
    if (!r) {
        snprintf(buf, sizeof buf, "%s", SDL_GetError());
        check(0, name, buf);
        return;
    }
    SDL_SetRenderDrawColor(r, 200, 40, 20, 255);
    SDL_RenderClear(r);
    SDL_FRect rect = { 10, 10, 20, 20 };
    SDL_SetRenderDrawColor(r, 20, 200, 40, 255);
    SDL_RenderFillRect(r, &rect);
    SDL_Rect probe = { 15, 15, 1, 1 };
    SDL_Surface *px = SDL_RenderReadPixels(r, &probe);
    Uint8 cr = 0, cg = 0, cb = 0, ca = 0;
    if (px)
        SDL_ReadSurfacePixel(px, 0, 0, &cr, &cg, &cb, &ca);
    Uint64 t0 = SDL_GetTicksNS();
    int frames = 0;
    while (frames < 30) {
        SDL_RenderClear(r);
        SDL_RenderFillRect(r, &rect);
        SDL_RenderPresent(r);
        frames++;
    }
    double ms = (SDL_GetTicksNS() - t0) / 1e6 / frames;
    snprintf(buf, sizeof buf, "renderer %s, pixel %u,%u,%u, %.2f ms per frame", SDL_GetRendererName(r), cr, cg, cb, ms);
    check(px && cg > 150 && cr < 100, name, buf);
    SDL_DestroySurface(px);
    SDL_DestroyRenderer(r);
}

static void try_image(void)
{
    SDL_Surface *s = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
    SDL_FillSurfaceRect(s, NULL, SDL_MapSurfaceRGBA(s, 1, 2, 3, 255));
    SDL_IOStream *mem = SDL_IOFromDynamicMem();
    int saved = IMG_SavePNG_IO(s, mem, false);
    Sint64 size = SDL_GetIOSize(mem);
    SDL_SeekIO(mem, 0, SDL_IO_SEEK_SET);
    SDL_Surface *back = saved ? IMG_Load_IO(mem, false) : NULL;
    Uint8 r = 0, g = 0, b = 0, a = 0;
    if (back)
        SDL_ReadSurfacePixel(back, 3, 3, &r, &g, &b, &a);
    char buf[80];
    snprintf(buf, sizeof buf, "PNG %lld bytes, pixel %u,%u,%u", (long long)size, r, g, b);
    check(back && r == 1 && g == 2 && b == 3, "image", buf);
    SDL_DestroySurface(back);
    SDL_CloseIO(mem);
    SDL_DestroySurface(s);
}

static void try_iconv(void)
{
    const char utf8[] = "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 \xc3\xa9";   /* Zażółć é */
    char *latin1 = SDL_iconv_string("ISO-8859-1", "UTF-8", "caf\xc3\xa9", 6);
    char *latin2 = SDL_iconv_string("ISO-8859-2", "UTF-8", utf8, sizeof utf8);
    int ok = latin1 && (unsigned char)latin1[3] == 0xe9 && latin2 && (unsigned char)latin2[2] == 0xbf;
    check(ok, "iconv", ok ? "UTF-8 to ISO-8859-1 and ISO-8859-2" : SDL_GetError());
    SDL_free(latin1);
    SDL_free(latin2);
}

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    int seconds = argc > 2 ? atoi(argv[2]) : 0;
    int ww = argc > 3 ? atoi(argv[3]) : 320, wh = argc > 4 ? atoi(argv[4]) : 200;
    printf("SDL %d.%d.%d, SDL3_image %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION,
           SDL_IMAGE_MAJOR_VERSION, SDL_IMAGE_MINOR_VERSION, SDL_IMAGE_MICRO_VERSION);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        check(0, "init", SDL_GetError());
        puts("result: fail");
        return 20;
    }
    check(1, "init", SDL_GetCurrentVideoDriver());
    try_iconv();
    try_image();
    SDL_Window *win = SDL_CreateWindow("aros-sdl self-test", ww, wh, SDL_WINDOW_RESIZABLE);
    check(win != NULL, "window", win ? "" : SDL_GetError());
    if (win) {
        int w, h, pw, ph, x, y;
        SDL_Rect usable = { 0, 0, 0, 0 };
        SDL_GetWindowSize(win, &w, &h);
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        SDL_GetWindowPosition(win, &x, &y);
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win), &usable);
        printf("geometry: asked %dx%d, size %dx%d, pixels %dx%d, position %d,%d, usable %d,%d %dx%d\n",
               ww, wh, w, h, pw, ph, x, y, usable.x, usable.y, usable.w, usable.h);
    }
    if (win) {
        if (!strcmp(which, "software") || !strcmp(which, "all"))
            try_renderer(win, "software");
        if (!strcmp(which, "opengl") || !strcmp(which, "all"))
            try_renderer(win, "opengl");
        if (seconds > 0) {
            SDL_Renderer *r = SDL_CreateRenderer(win, strcmp(which, "all") ? which : NULL);
            printf("interactive renderer: %s\n", r ? SDL_GetRendererName(r) : SDL_GetError());
            Uint64 frames = 0, t0 = SDL_GetTicksNS();
            Uint64 end = SDL_GetTicks() + (Uint64)seconds * 1000;
            int quit = 0, motions = 0;
            SDL_StartTextInput(win);
            while (!quit && SDL_GetTicks() < end) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                        quit = 1;
                    else if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
                        printf("window event %u (%d,%d)\n", (unsigned)e.type, e.window.data1, e.window.data2);
                    else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
                        printf("mouse button %s %u at %.0f,%.0f\n", e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "down" : "up",
                               (unsigned)e.button.button, e.button.x, e.button.y);
                    else if (e.type == SDL_EVENT_MOUSE_MOTION)
                        motions++;
                    else if (e.type == SDL_EVENT_KEY_DOWN)
                        printf("key down scancode %d key 0x%x\n", (int)e.key.scancode, (unsigned)e.key.key);
                    else if (e.type == SDL_EVENT_TEXT_INPUT)
                        printf("text \"%s\"\n", e.text.text);
                }
                if (r) {
                    /* A blue background with a moving orange bar: visible on a
                       screenshot whichever renderer draws it. */
                    int ww2, wh2;
                    SDL_GetWindowSize(win, &ww2, &wh2);
                    SDL_SetRenderDrawColor(r, 20, 60, 120, 255);
                    SDL_RenderClear(r);
                    SDL_FRect bar = { (float)((frames * 8) % (Uint64)(ww2 > 40 ? ww2 - 40 : 1)), (float)(wh2 / 3), 40.0f, (float)(wh2 / 3) };
                    SDL_SetRenderDrawColor(r, 230, 120, 30, 255);
                    SDL_RenderFillRect(r, &bar);
                    SDL_RenderPresent(r);
                    frames++;
                }
                SDL_Delay(20);
            }
            printf("mouse motion events: %d\n", motions);
            if (frames)
                printf("interactive frames: %llu, %.2f ms per frame\n", (unsigned long long)frames,
                       (SDL_GetTicksNS() - t0) / 1e6 / (double)frames);
            SDL_DestroyRenderer(r);
        }
        SDL_DestroyWindow(win);
    }
    SDL_Quit();
    puts(failures ? "result: fail" : "result: pass");
    return failures ? 10 : 0;
}
