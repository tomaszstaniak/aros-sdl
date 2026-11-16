/* Native check: does Intuition report the iconify title bar gadget?
 *   iconify-gadget [seconds]
 * Opens a window with WA_ExtraTitlebarGadgets ETG_ICONIFY and prints every
 * IDCMP message class (and the gadget ID for IDCMP_GADGETUP).
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/extensions.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 30;
    struct Window *w = OpenWindowTags(NULL,
        WA_Left, 240, WA_Top, 200, WA_InnerWidth, 320, WA_InnerHeight, 120,
        WA_Title, (IPTR)"iconify gadget test",
        WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIMPLE_REFRESH,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MOUSEBUTTONS | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW | IDCMP_REFRESHWINDOW,
        WA_ExtraTitlebarGadgets, ETG_ICONIFY,
        TAG_DONE);
    if (!w) {
        printf("OpenWindow failed\n");
        return 20;
    }
    printf("window open, ETI_Iconify = 0x%x\n", (unsigned)ETI_Iconify);
    int quit = 0, ticks = seconds * 10;
    while (!quit && ticks-- > 0) {
        struct IntuiMessage *m;
        while ((m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            ULONG cl = m->Class;
            if (cl == IDCMP_GADGETUP || cl == IDCMP_GADGETDOWN)
                printf("class 0x%lx gadget id 0x%x\n", (unsigned long)cl, (unsigned)((struct Gadget *)m->IAddress)->GadgetID);
            else
                printf("class 0x%lx code 0x%x\n", (unsigned long)cl, (unsigned)m->Code);
            if (cl == IDCMP_REFRESHWINDOW) {
                BeginRefresh(w);
                EndRefresh(w, TRUE);
            }
            if (cl == IDCMP_CLOSEWINDOW)
                quit = 1;
            ReplyMsg((struct Message *)m);
        }
        Delay(5);
    }
    CloseWindow(w);
    printf("done\n");
    return 0;
}
