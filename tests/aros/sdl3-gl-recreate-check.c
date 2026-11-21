/* OpenGL keeps drawing into a window that SDL opens again.
 *
 *   sdl3-gl-recreate-check [seconds]
 *
 * The AROS backend closes and reopens the Intuition window when a property
 * that Intuition cannot change in place is set (resizable, borderless, always
 * on top, fullscreen). The OpenGL context has to follow to the new window.
 * The program fills the window with one colour, changes a property, fills it
 * with another colour and reads the pixel in the middle of the window back
 * from the screen. With [seconds] the last frame stays up for that long.
 * Prints one line per step and "result: pass" or "result: fail".
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <intuition/screens.h>

#include <stdio.h>
#include <stdlib.h>

struct Library *CyberGfxBase;

static int failures;

/* Fills the window, shows the frame and compares the pixel on the screen. */
static void step(SDL_Window *win, const char *what, int r, int g, int b)
{
    struct Screen *scr;
    ULONG px = 0;
    int x, y, w, h, i, pr, pg, pb, ok;

    for (i = 0; i < 3; i++) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
        }
        SDL_GetWindowSizeInPixels(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(win);
        SDL_Delay(100);
    }

    SDL_GetWindowPosition(win, &x, &y);
    SDL_GetWindowSize(win, &w, &h);
    scr = LockPubScreen(NULL);
    if (scr) {
        px = ReadRGBPixel(&scr->RastPort, x + w / 2, y + h / 2);
        UnlockPubScreen(NULL, scr);
    }
    pr = (px >> 16) & 255;
    pg = (px >> 8) & 255;
    pb = px & 255;
    ok = scr && abs(pr - r) < 24 && abs(pg - g) < 24 && abs(pb - b) < 24;
    printf("%s: %s - drew %d,%d,%d, screen has %d,%d,%d at %d,%d\n", what, ok ? "ok" : "FAIL",
           r, g, b, pr, pg, pb, x + w / 2, y + h / 2);
    if (!ok)
        failures++;
}

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_GLContext ctx;
    int seconds = argc > 1 ? atoi(argv[1]) : 0;

    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 0);
    if (!CyberGfxBase) {
        printf("cybergraphics.library: FAIL\nresult: fail\n");
        return 20;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init: FAIL - %s\nresult: fail\n", SDL_GetError());
        return 20;
    }
    win = SDL_CreateWindow("sdl3-gl-recreate-check", 320, 240, SDL_WINDOW_OPENGL);
    ctx = win ? SDL_GL_CreateContext(win) : NULL;
    if (!ctx) {
        printf("window and context: FAIL - %s\nresult: fail\n", SDL_GetError());
        SDL_Quit();
        return 20;
    }
    printf("renderer: %s\n", (const char *)glGetString(GL_RENDERER));

    step(win, "first window", 200, 40, 20);
    SDL_SetWindowResizable(win, true);
    step(win, "after resizable on", 20, 200, 40);
    SDL_SetWindowBordered(win, false);
    step(win, "after border off", 20, 40, 200);
    SDL_SetWindowBordered(win, true);
    SDL_SetWindowResizable(win, false);
    step(win, "after border on, resizable off", 200, 200, 20);

    if (seconds > 0)
        SDL_Delay(seconds * 1000);

    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    CloseLibrary(CyberGfxBase);
    printf("result: %s\n", failures ? "fail" : "pass");
    return failures ? 20 : 0;
}
