#ifndef WAV_WRITER_H
#define WAV_WRITER_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>   /* FILE */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FILE*   fp;
    int32_t sample_rate;
    int64_t sample_count;
} wav_writer_t;

/* 16bit mono WAV を書き出すユーティリティ */

/* 一括書き込み */
int wav_write_mono16(const char* path, const int16_t* samples, size_t count, int sample_rate);

/* 逐次書き込み用（今は使わなくてもよい） */
int  wav_writer_open(wav_writer_t* w, const char* path, int sample_rate);
int  wav_writer_write(wav_writer_t* w, const int16_t* samples, size_t count);
int  wav_writer_close(wav_writer_t* w);

#ifdef __cplusplus
}
#endif

#endif /* WAV_WRITER_H */