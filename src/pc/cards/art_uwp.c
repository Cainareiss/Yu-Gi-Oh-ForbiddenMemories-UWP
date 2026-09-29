#include "art.h"
#include <stddef.h>
#include <stdio.h>

int CardArt_FromImage(const char *path, unsigned char *record, char *why, size_t why_size)
{
    (void)path; (void)record;
    if (why && why_size) snprintf(why, why_size, "PNG art not supported on UWP core");
    return 0;
}

int CardArt_ThumbnailFromImage(const char *path, unsigned char *record, char *why, size_t why_size)
{
    (void)path; (void)record;
    if (why && why_size) snprintf(why, why_size, "PNG art not supported on UWP core");
    return 0;
}

int CardArt_Crop(const char *path, int w, int h, int *x, int *y, int *cw, int *ch, int *width, int *height)
{
    (void)path; (void)w; (void)h; (void)x; (void)y; (void)cw; (void)ch; (void)width; (void)height;
    return 0;
}

int CardArt_TitleFromImage(const char *path, unsigned char *plate, char *why, size_t why_size)
{
    (void)path; (void)plate;
    if (why && why_size) snprintf(why, why_size, "PNG art not supported on UWP core");
    return 0;
}

int CardArt_PortraitFromImage(const char *path, unsigned char *record, char *why, size_t why_size)
{
    (void)path; (void)record;
    if (why && why_size) snprintf(why, why_size, "PNG art not supported on UWP core");
    return 0;
}

int CardArt_TitleFromName(const char *name, unsigned char *plate)
{
    (void)name; (void)plate;
    return 0;
}

int CardArt_TitlePicture(const char *name, int factor, unsigned char *indices, int pitch)
{
    (void)name; (void)factor; (void)indices; (void)pitch;
    return 0;
}

void *CardArt_SerifFace(void)
{
    return NULL;
}
