#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <time.h>
#include <liquid/liquid.h>

#define NUM_IDLE_PREAMBLE    4
#define NUM_IDLE_POSTAMBLE   4
#define NUM_HEADER_REPEATS   5
#define HEADER_PAYLOAD_LEN   16
#define IDLE_CHAR            0x55

void calc_sizes(unsigned int payload_len, unsigned int *enc_len, unsigned int *symbols)
{
    packetizer p = packetizer_create(payload_len, LIQUID_CRC_32,
                                     LIQUID_FEC_NONE, LIQUID_FEC_NONE);
    *enc_len = packetizer_get_enc_msg_len(p);
    *symbols = (*enc_len * 8) / 2;
    packetizer_destroy(p);
}

unsigned int encode_and_modulate(
    unsigned char *payload, unsigned int payload_len,
    float complex *out_buffer, unsigned int out_capacity)
{
    packetizer p = packetizer_create(payload_len, LIQUID_CRC_32,
                                     LIQUID_FEC_NONE, LIQUID_FEC_NONE);
    unsigned int enc_len = packetizer_get_enc_msg_len(p);
    unsigned char *enc = (unsigned char *)malloc(enc_len);
    packetizer_encode(p, payload, enc);

    modem mod = modem_create(LIQUID_MODEM_QPSK);
    unsigned int num_bits = enc_len * 8;
    unsigned int num_symbols = num_bits / 2;

    if (num_symbols > out_capacity) {
        fprintf(stderr, "encode_and_modulate: output buffer too small\n");
        num_symbols = out_capacity;
    }

    for (unsigned int i = 0; i < enc_len; i++) {
        unsigned char bv = enc[i];
        for (int b = 6; b >= 0; b -= 2) {
            unsigned int pair = (bv >> b) & 0x03;
            float complex sym;
            modem_modulate(mod, pair, &sym);
            out_buffer[i * 4 + (3 - b/2)] = sym; // 每个字节4个QPSK符号
        }
    }

    modem_destroy(mod);
    packetizer_destroy(p);
    free(enc);

    return num_symbols;
}

int decode_packet_at(modem mod_rx, float complex *rx_symbols, unsigned int rx_sym_count,
                     unsigned int start_sym, unsigned int enc_len,
                     unsigned int payload_len, unsigned char *payload_out,
                     unsigned int *sym_consumed)
{
    unsigned int symbols_per_packet = (enc_len * 8) / 2;
    if (start_sym + symbols_per_packet > rx_sym_count) return 0;

    unsigned char *bits = (unsigned char *)malloc(enc_len);
    unsigned int cursor = start_sym;

    for (unsigned int i = 0; i < enc_len; i++) {
        unsigned char bv = 0;
        for (int b = 6; b >= 0; b -= 2) {
            float complex sym = rx_symbols[cursor++];
            unsigned int pair;
            modem_demodulate(mod_rx, sym, &pair);
            bv |= (pair << b);
        }
        bits[i] = bv;
    }

    packetizer p = packetizer_create(payload_len, LIQUID_CRC_32,
                                     LIQUID_FEC_NONE, LIQUID_FEC_NONE);
    int ok = packetizer_decode(p, bits, payload_out);
    packetizer_destroy(p);
    free(bits);

    if (sym_consumed) *sym_consumed = cursor - start_sym;
    return ok;
}

int main() {
    srand((unsigned int)time(NULL));

    printf("[TX] Loading test.png...\n");
    FILE *fin = fopen("test.png", "rb");
    if (!fin) {
        fprintf(stderr, "Error: unable to open test.png for reading.\n");
        return 1;
    }
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    unsigned char *payload = (unsigned char *)malloc(file_size);
    fread(payload, 1, file_size, fin);
    fclose(fin);
    printf("[TX] Loaded successfully. Size = %ld bytes.\n", file_size);

    unsigned char idle_payload[HEADER_PAYLOAD_LEN];
    memset(idle_payload, IDLE_CHAR, HEADER_PAYLOAD_LEN);

    unsigned char header_payload[HEADER_PAYLOAD_LEN];
    memset(header_payload, 0, HEADER_PAYLOAD_LEN);

    unsigned int file_crc = crc_generate_key(LIQUID_CRC_32, payload, file_size);
    memcpy(header_payload, &file_size, sizeof(long));
    memcpy(header_payload + 8, &file_crc, sizeof(unsigned int));
    printf("[TX] File size = %ld, CRC-32 = 0x%08X\n", file_size, file_crc);

    unsigned int idle_enc_len, idle_symbols;
    calc_sizes(HEADER_PAYLOAD_LEN, &idle_enc_len, &idle_symbols);

    unsigned int header_enc_len, header_symbols;
    calc_sizes(HEADER_PAYLOAD_LEN, &header_enc_len, &header_symbols);

    unsigned int file_enc_len, file_symbols;
    calc_sizes(file_size, &file_enc_len, &file_symbols);

    unsigned int total_symbols =
        NUM_IDLE_PREAMBLE  * idle_symbols +
        NUM_HEADER_REPEATS * header_symbols +
        file_symbols +
        NUM_IDLE_POSTAMBLE * idle_symbols;

    float complex *tx_symbols = (float complex *)malloc(total_symbols * sizeof(float complex));
    unsigned int sym_offset = 0;

    for (int i = 0; i < NUM_IDLE_PREAMBLE; i++) {
        sym_offset += encode_and_modulate(idle_payload, HEADER_PAYLOAD_LEN,
                                          tx_symbols + sym_offset, total_symbols - sym_offset);
    }
    for (int i = 0; i < NUM_HEADER_REPEATS; i++) {
        sym_offset += encode_and_modulate(header_payload, HEADER_PAYLOAD_LEN,
                                          tx_symbols + sym_offset, total_symbols - sym_offset);
    }
    sym_offset += encode_and_modulate(payload, file_size,
                                      tx_symbols + sym_offset, total_symbols - sym_offset);
    for (int i = 0; i < NUM_IDLE_POSTAMBLE; i++) {
        sym_offset += encode_and_modulate(idle_payload, HEADER_PAYLOAD_LEN,
                                          tx_symbols + sym_offset, total_symbols - sym_offset);
    }
    printf("[TX] Total generated symbols: %u\n", sym_offset);

    // 模拟理想信道（直接把符号流传给接收端）
    float complex *rx_symbols = tx_symbols;
    unsigned int rx_sym_count = sym_offset;

    modem mod_rx = modem_create(LIQUID_MODEM_QPSK);

    printf("[RX] Searching for exact frame boundary...\n");
    int found_offset = -1;
    unsigned char test_out[HEADER_PAYLOAD_LEN];

    // 精准寻找前导闲置包的起始位置
    for (unsigned int offset = 0; offset < rx_sym_count; offset++) {
        if (decode_packet_at(mod_rx, rx_symbols, rx_sym_count,
                             offset, idle_enc_len, HEADER_PAYLOAD_LEN,
                             test_out, NULL)) {
            found_offset = offset;
            printf("[RX] Found frame boundary at symbol offset: %u\n", offset);
            break;
        }
    }

    if (found_offset < 0) {
        printf("[RX] FAILED: cannot find frame boundary.\n");
        goto cleanup;
    }

    unsigned int sym_cursor = found_offset;

    // 跳过前导空闲包
    for (int i = 0; i < NUM_IDLE_PREAMBLE; i++) {
        unsigned int consumed = 0;
        decode_packet_at(mod_rx, rx_symbols, rx_sym_count,
                         sym_cursor, idle_enc_len, HEADER_PAYLOAD_LEN,
                         test_out, &consumed);
        sym_cursor += consumed;
    }

    // 解析包头
    int header_ok = 0;
    long file_size_rx = 0;
    unsigned int file_crc_rx = 0;

    for (int rep = 0; rep < NUM_HEADER_REPEATS; rep++) {
        unsigned char tmp[HEADER_PAYLOAD_LEN];
        unsigned int consumed = 0;
        int ok = decode_packet_at(mod_rx, rx_symbols, rx_sym_count,
                                  sym_cursor, header_enc_len, HEADER_PAYLOAD_LEN,
                                  tmp, &consumed);
        sym_cursor += consumed;

        if (ok && !header_ok) {
            memcpy(&file_size_rx, tmp, sizeof(long));
            memcpy(&file_crc_rx, tmp + 8, sizeof(unsigned int));
            header_ok = 1;
            printf("[RX] Header decoded OK: size=%ld, CRC=0x%08X\n",
                   file_size_rx, file_crc_rx);
        }
    }

    if (!header_ok) {
        printf("[RX] FAILED: no valid header decoded.\n");
        goto cleanup;
    }

    // 解包文件主体
    {
        unsigned char *payload_rx = (unsigned char *)malloc(file_size_rx);
        unsigned int consumed = 0;
        int crc_pass = decode_packet_at(mod_rx, rx_symbols, rx_sym_count,
                                       sym_cursor, file_enc_len, file_size_rx,
                                       payload_rx, &consumed);

        if (crc_pass) {
            printf("[RX] SUCCESS! File CRC check passed. recovered.png generated.\n");
            FILE *fout = fopen("recovered.png", "wb");
            if (fout) {
                fwrite(payload_rx, 1, file_size_rx, fout);
                fclose(fout);
            }
        } else {
            printf("[RX] FAILED! File CRC check failed.\n");
        }
        free(payload_rx);
    }

cleanup:
    free(payload);
    free(tx_symbols);
    modem_destroy(mod_rx);

    return 0;
}
