#ifndef __AUDIO_TONEURI_H__
#define __AUDIO_TONEURI_H__

extern const char* tone_uri[];

typedef enum {
    TONE_TYPE_DING,
    TONE_TYPE_FSTART,
    TONE_TYPE_HELLO,
    TONE_TYPE_START,
    TONE_TYPE_STOP,
    TONE_TYPE_VSTART,
    TONE_TYPE_MAX,
} tone_type_t;

int get_tone_uri_num();

#endif
