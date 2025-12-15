#include "wav_writer.h"

#include <stdio.h>
#include <string.h>

/* RIFF/WAVE ヘッダを書き出す */
static void write_wav_header(FILE* fp, int32_t sample_rate, int32_t num_samples)
{
    /* 16bit mono */
    int16_t num_channels = 1;
    int16_t bits_per_sample = 16;
    int16_t block_align = num_channels * (bits_per_sample / 8);
    int32_t byte_rate = sample_rate * block_align;
    int32_t data_size = num_samples * block_align;
    int32_t chunk_size = 36 + data_size;

    /* RIFF chunk */
    fwrite("RIFF", 1, 4, fp);
    fwrite(&chunk_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, fp);
    int32_t subchunk1_size = 16;
    int16_t audio_format = 1; /* PCM */
    fwrite(&subchunk1_size, 4, 1, fp);
    fwrite(&audio_format, 2, 1, fp);
    fwrite(&num_channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits_per_sample, 2, 1, fp);

    /* data chunk header */
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);
}

/* 一括書き込み用 */
int wav_write_mono16(const char* path, const int16_t* samples, size_t count, int sample_rate)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[wav_writer] cannot open: %s\n", path);
        return -1;
    }

    write_wav_header(fp, sample_rate, (int32_t)count);

    if (count > 0 && samples) {
        fwrite(samples, sizeof(int16_t), count, fp);
    }

    fclose(fp);
    return 0;
}

/* 逐次書き込み API（今回は使わないが一応実装） */
int wav_writer_open(wav_writer_t* w, const char* path, int sample_rate)
{
    if (!w) return -1;
    memset(w, 0, sizeof(*w));
    w->fp = fopen(path, "wb+");
    if (!w->fp) {
        fprintf(stderr, "[wav_writer] cannot open: %s\n", path);
        return -1;
    }
    w->sample_rate  = sample_rate;
    w->sample_count = 0;
    return 0;
}

int wav_writer_write(wav_writer_t* w, const int16_t* samples, size_t count)
{
    if (!w || !w->fp || !samples) return -1;
    size_t n = fwrite(samples, sizeof(int16_t), count, w->fp);
    w->sample_count += (int64_t)n;
    return 0;
}

int wav_writer_close(wav_writer_t* w)
{
    if (!w || !w->fp) return -1;
    fclose(w->fp);
    w->fp = NULL;
    return 0;
}