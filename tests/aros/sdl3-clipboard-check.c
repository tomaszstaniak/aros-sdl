/* Checks the text SDL3 puts on the AROS clipboard.
 *
 *   sdl3-clipboard-check
 *
 * Copies a text with SDL_SetClipboardText, then looks at clipboard unit 0
 * three ways: the raw bytes through clipboard.device, the CHRS chunk through
 * iffparse.library (as another program reads it), and SDL_GetClipboardText.
 * Prints one line per check and "result: pass" or "result: fail". The text
 * stays on the clipboard; nothing else is created. It can be run again in
 * the same boot.
 */
#include <SDL3/SDL.h>

#include <exec/types.h>
#include <exec/io.h>
#include <devices/clipboard.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/iffparse.h>

#include <stdio.h>
#include <string.h>

#define ID_FTXT MAKE_ID('F','T','X','T')
#define ID_CHRS MAKE_ID('C','H','R','S')

static const char text[] = "SDL3 clipboard check 0123";
static int failures;

static void check(int ok, const char *what, const char *detail)
{
    printf("%s: %s - %s\n", what, ok ? "ok" : "FAIL", detail);
    if (!ok)
        failures++;
}

/* The first bytes of clipboard unit 0, read through clipboard.device. */
static ULONG read_raw(UBYTE *buf, ULONG size)
{
    struct MsgPort *mp = CreateMsgPort();
    struct IOClipReq *io = mp ? (struct IOClipReq *)CreateIORequest(mp, sizeof(struct IOClipReq)) : NULL;
    ULONG got = 0;

    if (io && !OpenDevice((CONST_STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest *)io, 0)) {
        static UBYTE sink[256];

        io->io_Offset = 0;
        io->io_ClipID = 0;
        io->io_Command = CMD_READ;
        io->io_Data = (STRPTR)buf;
        io->io_Length = size;
        DoIO((struct IORequest *)io);
        got = io->io_Actual;

        /* Reading past the end tells the device the read is finished. */
        do {
            io->io_Command = CMD_READ;
            io->io_Data = (STRPTR)sink;
            io->io_Length = sizeof sink;
            DoIO((struct IORequest *)io);
        } while (io->io_Actual > 0);

        CloseDevice((struct IORequest *)io);
    }
    if (io)
        DeleteIORequest((struct IORequest *)io);
    if (mp)
        DeleteMsgPort(mp);
    return got;
}

/* The CHRS chunk of the FTXT on the clipboard, read with iffparse.library.
   Returns its length, or -1 with the reason in why. */
static LONG read_chrs(char *out, LONG size, const char **why)
{
    struct IFFHandle *iff = AllocIFF();
    LONG len = -1;

    *why = "AllocIFF failed";
    if (!iff)
        return -1;

    iff->iff_Stream = (IPTR)OpenClipboard(PRIMARY_CLIP);
    *why = "OpenClipboard failed";
    if (iff->iff_Stream) {
        InitIFFasClip(iff);
        *why = "OpenIFF failed";
        if (!OpenIFF(iff, IFFF_READ)) {
            *why = "no CHRS chunk in a FORM FTXT";
            if (!StopChunk(iff, ID_FTXT, ID_CHRS) && !ParseIFF(iff, IFFPARSE_SCAN)) {
                len = ReadChunkBytes(iff, out, size - 1);
                if (len >= 0)
                    out[len] = '\0';
                else
                    *why = "ReadChunkBytes failed";
            }
            CloseIFF(iff);
        }
        CloseClipboard((struct ClipboardHandle *)iff->iff_Stream);
    }
    FreeIFF(iff);
    return len;
}

int main(void)
{
    char detail[256], chunk[128];
    UBYTE raw[64];
    const char *why = "";
    ULONG got, i;
    LONG len;
    char *back;

    printf("SDL %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        check(0, "init", SDL_GetError());
        puts("result: fail");
        return 10;
    }

    check(SDL_SetClipboardText(text), "SDL_SetClipboardText", text);

    got = read_raw(raw, sizeof raw);
    strcpy(detail, "clipboard starts ");
    for (i = 0; i < got && i < 24; i++)
        sprintf(detail + strlen(detail), "%02x", raw[i]);
    check(got >= 12 && !memcmp(raw, "FORM", 4) && !memcmp(raw + 8, "FTXT", 4), "IFF header", detail);

    len = read_chrs(chunk, sizeof chunk, &why);
    if (len >= 0)
        snprintf(detail, sizeof detail, "CHRS chunk \"%s\" (%ld bytes)", chunk, (long)len);
    else
        snprintf(detail, sizeof detail, "%s", why);
    check(len == (LONG)strlen(text) && !strcmp(chunk, text), "read through iffparse.library", detail);

    back = SDL_GetClipboardText();
    snprintf(detail, sizeof detail, "\"%s\"", back ? back : "(null)");
    check(back && !strcmp(back, text), "SDL_GetClipboardText", detail);
    SDL_free(back);

    SDL_Quit();
    puts(failures ? "result: fail" : "result: pass");
    return failures ? 10 : 0;
}
