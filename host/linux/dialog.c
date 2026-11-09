#include "../ui/ui.h"

#include <SDL3/SDL.h>

/* SDL shows a message box without being brought up, so the client that starts the machine can use it. */
void mime_ui_alert(const char *text)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mime", text, nullptr);
}
