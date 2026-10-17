/*
 * the resolutions a display lists below its own, as a panel's scaler
 * takes them: what games show in their resolution menus. win32k's display
 * modes and dxgi's outputs both list them.
 */
#ifndef MIME_DISPLAY_MODES_H
#define MIME_DISPLAY_MODES_H

static const struct {
    unsigned short width, height;
} MIME_DISPLAY_RESOLUTIONS[] = {
    {640, 480},   {720, 480},   {720, 576},   {800, 600},   {1024, 768},  {1152, 864},  {1176, 664},
    {1280, 720},  {1280, 768},  {1280, 800},  {1280, 960},  {1280, 1024}, {1360, 768},  {1366, 768},
    {1440, 900},  {1600, 900},  {1600, 1024}, {1600, 1200}, {1680, 1050}, {1920, 1080}, {1920, 1200},
    {2048, 1152}, {2560, 1080}, {2560, 1440}, {2560, 1600}, {3440, 1440}, {3840, 2160},
};

#endif
