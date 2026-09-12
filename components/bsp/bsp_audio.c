/**
 * @file bsp_audio.c
 * @brief I2S audio output driving the onboard 2.5W/4R class-D amplifier.
 *
 * The board only wires up BCLK/WS/DOUT (no MCLK, no microphone), so this is
 * a TX-only I2S "standard" (Philips) channel. Two playback paths are
 * provided: a synthesized beep (used for the boot chime / UI clicks) and a
 * simple 16/8-bit PCM .wav file player for custom sounds placed on the SD
 * card.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2s_std.h"

#include "bsp/bsp_pins.h"
#include "bsp/bsp_board.h"

static const char *TAG = "bsp_audio";

static i2s_chan_handle_t s_tx_chan = NULL;
static bool s_muted = false;

/* Minimal canonical WAV header (44 bytes). Extra chunks before "data" are
 * skipped by app_photo-style linear scanning below. */
typedef struct __attribute__((packed)) {
    char     riff_tag[4];
    uint32_t riff_size;
    char     wave_tag[4];
} wav_riff_hdr_t;

typedef struct __attribute__((packed)) {
    char     chunk_id[4];
    uint32_t chunk_size;
} wav_chunk_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_fmt_chunk_t;

static esp_err_t bsp_audio_reconfigure(uint32_t sample_rate, i2s_slot_mode_t slot_mode,
                                        i2s_data_bit_width_t bits)
{
    esp_err_t ret = i2s_channel_disable(s_tx_chan);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, slot_mode);

    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(s_tx_chan, &slot_cfg));
    return i2s_channel_enable(s_tx_chan);
}

esp_err_t bsp_audio_init(void)
{
    const i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT, I2S_ROLE_MASTER);
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BSP_I2S_PIN_BCLK,
            .ws = BSP_I2S_PIN_WS,
            .dout = BSP_I2S_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return i2s_channel_enable(s_tx_chan);
}

void bsp_audio_set_mute(bool mute)
{
    s_muted = mute;
}

esp_err_t bsp_audio_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_muted || freq_hz == 0 || duration_ms == 0) {
        return ESP_OK;
    }

    const uint32_t sample_rate = 44100;
    ESP_RETURN_ON_ERROR(bsp_audio_reconfigure(sample_rate, I2S_SLOT_MODE_STEREO, I2S_DATA_BIT_WIDTH_16BIT),
                         TAG, "reconfigure failed");

    /* One period buffer, streamed repeatedly - keeps RAM use tiny regardless
     * of the requested duration. */
    const int samples_per_period = sample_rate / freq_hz;
    const int frame_count = samples_per_period > 0 ? samples_per_period : 1;
    int16_t *buf = (int16_t *)malloc(frame_count * 2 * sizeof(int16_t));
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    for (int frame_i = 0; frame_i < frame_count; frame_i++) {
        int16_t sample = (int16_t)(9000.0f * sinf(2.0f * (float)M_PI * frame_i / frame_count));
        buf[2 * frame_i] = sample;
        buf[2 * frame_i + 1] = sample;
    }

    uint32_t elapsed_ms = 0;
    size_t bytes_written = 0;
    const uint32_t period_ms = (frame_count * 1000) / sample_rate + 1;
    while (elapsed_ms < duration_ms) {
        i2s_channel_write(s_tx_chan, buf, frame_count * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
        elapsed_ms += period_ms;
    }

    free(buf);
    return ESP_OK;
}

esp_err_t bsp_audio_play_wav(const char *path)
{
    if (s_tx_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_muted) {
        return ESP_OK;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "Could not open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    wav_riff_hdr_t riff_hdr;
    if (fread(&riff_hdr, sizeof(riff_hdr), 1, f) != 1 ||
        memcmp(riff_hdr.riff_tag, "RIFF", 4) != 0 ||
        memcmp(riff_hdr.wave_tag, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "%s is not a RIFF/WAVE file", path);
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }

    wav_fmt_chunk_t fmt = {0};
    bool have_fmt = false;
    long data_offset = -1;
    uint32_t data_size = 0;

    wav_chunk_hdr_t chunk;
    while (fread(&chunk, sizeof(chunk), 1, f) == 1) {
        if (memcmp(chunk.chunk_id, "fmt ", 4) == 0) {
            fread(&fmt, sizeof(fmt), 1, f);
            have_fmt = true;
            if (chunk.chunk_size > sizeof(fmt)) {
                fseek(f, chunk.chunk_size - sizeof(fmt), SEEK_CUR);
            }
        } else if (memcmp(chunk.chunk_id, "data", 4) == 0) {
            data_offset = ftell(f);
            data_size = chunk.chunk_size;
            break;
        } else {
            fseek(f, chunk.chunk_size, SEEK_CUR);
        }
    }

    if (!have_fmt || data_offset < 0) {
        ESP_LOGW(TAG, "%s: missing fmt/data chunk", path);
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }

    i2s_data_bit_width_t bits = (fmt.bits_per_sample == 8) ? I2S_DATA_BIT_WIDTH_8BIT : I2S_DATA_BIT_WIDTH_16BIT;
    i2s_slot_mode_t mode = (fmt.num_channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;

    esp_err_t ret = bsp_audio_reconfigure(fmt.sample_rate, mode, bits);
    if (ret != ESP_OK) {
        fclose(f);
        return ret;
    }

    fseek(f, data_offset, SEEK_SET);
    uint8_t *chunk_buf = malloc(2048);
    if (!chunk_buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    uint32_t remaining = data_size;
    while (remaining > 0) {
        size_t to_read = remaining < 2048 ? remaining : 2048;
        size_t got = fread(chunk_buf, 1, to_read, f);
        if (got == 0) {
            break;
        }
        size_t written = 0;
        i2s_channel_write(s_tx_chan, chunk_buf, got, &written, portMAX_DELAY);
        remaining -= got;
    }

    free(chunk_buf);
    fclose(f);
    return ESP_OK;
}
