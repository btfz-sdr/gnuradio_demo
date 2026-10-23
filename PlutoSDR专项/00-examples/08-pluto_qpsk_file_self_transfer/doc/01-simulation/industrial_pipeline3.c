#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <time.h>
#include <liquid/liquid.h>

#define PAYLOAD_CHUNK_SIZE  128  
#define SYNC_WORD           0xAA55AA55
#define STREAM_BLOCK_SIZE   1024  // 经典流式块大小

// 模拟空气信道损伤（包含动态衰减、CFO、相偏与噪声）
void apply_channel_impairments(float complex *buffer, unsigned int length, 
                               float cfo, float phase_init, float noise_sigma, float gain_scale) 
{
    for (unsigned int i = 0; i < length; i++) {
        buffer[i] *= gain_scale;

        float complex carrier = cexpf(_Complex_I * (phase_init + i * cfo));
        buffer[i] *= carrier;

        float u1 = (float)rand() / RAND_MAX;
        float u2 = (float)rand() / RAND_MAX;
        if (u1 < 1e-6f) u1 = 1e-6f;
        float n_real = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * M_PI * u2) * noise_sigma;
        float n_imag = sqrtf(-2.0f * logf(u1)) * sinf(2.0f * M_PI * u2) * noise_sigma;
        buffer[i] += n_real + _Complex_I * n_imag;
    }
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

    unsigned char *file_buffer = (unsigned char *)malloc(file_size);
    fread(file_buffer, 1, file_size, fin);
    fclose(fin);

    unsigned int num_chunks = (file_size + PAYLOAD_CHUNK_SIZE - 1) / PAYLOAD_CHUNK_SIZE;
    printf("[TX] Total file chunks: %u (File size: %ld bytes)\n", num_chunks, file_size);

    unsigned int packet_raw_len = 4 + 4 + PAYLOAD_CHUNK_SIZE;
    packetizer p_temp = packetizer_create(packet_raw_len, LIQUID_CRC_32, LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
    unsigned int enc_msg_len = packetizer_get_enc_msg_len(p_temp);
    packetizer_destroy(p_temp);

    unsigned int k = 2;       
    unsigned int m = 7;       
    float beta = 0.3f;        
    unsigned int h_len = 2 * k * m + 1;
    float h[h_len];
    liquid_firdes_prototype(LIQUID_FIRFILT_RRC, k, m, beta, 0.0f, h);

    firinterp_crcf interpolator = firinterp_crcf_create(k, h, h_len);
    modem mod_tx = modem_create(LIQUID_MODEM_QPSK);

    unsigned int symbols_per_packet = (enc_msg_len * 8) / 2;
    unsigned int samples_per_packet = symbols_per_packet * k;
    unsigned int tail_symbols = 50;
    unsigned int total_tx_samples = num_chunks * samples_per_packet + tail_symbols * k + h_len * 2;

    float complex *tx_buffer = (float complex *)calloc(total_tx_samples, sizeof(float complex));
    unsigned int tx_cursor = 0;

    for (unsigned int c = 0; c < num_chunks; c++) {
        unsigned int current_size = PAYLOAD_CHUNK_SIZE;
        if (c == num_chunks - 1) current_size = file_size - (c * PAYLOAD_CHUNK_SIZE);

        unsigned char raw_packet[packet_raw_len];
        unsigned int sync_val = SYNC_WORD;
        memcpy(raw_packet, &sync_val, 4);
        memcpy(raw_packet + 4, &c, 4);
        memset(raw_packet + 8, 0, PAYLOAD_CHUNK_SIZE);
        memcpy(raw_packet + 8, file_buffer + (c * PAYLOAD_CHUNK_SIZE), current_size);

        packetizer p = packetizer_create(packet_raw_len, LIQUID_CRC_32, LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
        unsigned char enc_packet[enc_msg_len];
        packetizer_encode(p, raw_packet, enc_packet);
        packetizer_destroy(p);

        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = enc_packet[i];
            for (int b = 6; b >= 0; b -= 2) {
                unsigned int pair = (bv >> b) & 0x03;
                float complex sym;
                modem_modulate(mod_tx, pair, &sym);
                firinterp_crcf_execute(interpolator, sym, &tx_buffer[tx_cursor]);
                tx_cursor += k;
            }
        }
    }
    for (unsigned int t = 0; t < tail_symbols; t++) {
        firinterp_crcf_execute(interpolator, 0.0f, &tx_buffer[tx_cursor]);
        tx_cursor += k;
    }

    firinterp_crcf_destroy(interpolator);
    modem_destroy(mod_tx);
    printf("[TX] Generated %u RF samples.\n", tx_cursor);

    apply_channel_impairments(tx_buffer, tx_cursor, 0.0001f, 0.05f, 0.02f, 0.35f);

    // ==========================================
    // 2. 接收端（RX）：流式块循环架构 (Streaming Loop)
    // ==========================================
    printf("[RX] Starting real-machine streaming receiver loop...\n");

    agc_crcf      agc     = agc_crcf_create();          // 修正为 agc_crcf
    symsync_crcf  symsync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, k, m, beta, 32);
    nco_crcf      nco_rx  = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_frequency(nco_rx, 0.0f);
    nco_crcf_set_phase(nco_rx, 0.0f);
    agc_crcf_set_bandwidth(agc, 1e-2f);                   // 修正函数前缀

    unsigned int max_rx_symbols = tx_cursor;
    float complex *rx_symbols = (float complex *)malloc(max_rx_symbols * sizeof(float complex));
    unsigned int rx_sym_count = 0;

    unsigned int stream_cursor = 0;
    while (stream_cursor < tx_cursor) {
        unsigned int block_size = STREAM_BLOCK_SIZE;
        if (stream_cursor + block_size > tx_cursor) {
            block_size = tx_cursor - stream_cursor;
        }

        // 未来真机替换点：iio_channel_read(rx0_i, block_in, block_size * sizeof(float complex));
        float complex *block_in = &tx_buffer[stream_cursor];
        stream_cursor += block_size;

        for (unsigned int i = 0; i < block_size; i++) {
            float complex agc_sym;
            agc_crcf_execute(agc, block_in[i], &agc_sym); // 修正函数前缀

            float complex y[2];
            unsigned int num_extracted = 0;
            symsync_crcf_execute(symsync, &agc_sym, 1, y, &num_extracted);

            if (num_extracted > 0) {
                for (unsigned int j = 0; j < num_extracted; j++) {
                    if (rx_sym_count < max_rx_symbols) {
                        rx_symbols[rx_sym_count++] = y[j];
                    }
                }
            }
        }
    }

    agc_crcf_destroy(agc); // 修正函数前缀
    symsync_crcf_destroy(symsync);
    printf("[RX] Streaming loop finished. Recovered %u symbols.\n", rx_sym_count);

    // ==========================================
    // 3. 帧锁定与分片解码
    // ==========================================
    modem mod_rx = modem_create(LIQUID_MODEM_QPSK);
    // 移除重复定义的类型声明，直接复用已有的变量
    unsigned int successful_chunks = 0;
    unsigned char *reassembled_file = (unsigned char *)malloc(file_size);
    memset(reassembled_file, 0, file_size);

    int locked = 0;
    unsigned int rx_cursor = 0;

    for (unsigned int offset = 0; offset < 200 && offset + symbols_per_packet <= rx_sym_count; offset++) {
        nco_crcf nco_test = nco_crcf_create(LIQUID_NCO);
        unsigned int temp_cursor = offset;
        unsigned char enc_packet_rx[enc_msg_len];

        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = 0;
            for (int b = 6; b >= 0; b -= 2) {
                float complex sym_corrected;
                nco_crcf_mix_down(nco_test, rx_symbols[temp_cursor++], &sym_corrected);
                nco_crcf_step(nco_test);
                unsigned int pair;
                modem_demodulate(mod_rx, sym_corrected, &pair);
                bv |= (pair << b);
            }
            enc_packet_rx[i] = bv;
        }
        nco_crcf_destroy(nco_test);

        unsigned char raw_packet_rx[packet_raw_len];
        packetizer p = packetizer_create(packet_raw_len, LIQUID_CRC_32, LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
        int crc_pass = packetizer_decode(p, enc_packet_rx, raw_packet_rx);
        packetizer_destroy(p);

        if (crc_pass) {
            unsigned int rx_sync = 0;
            memcpy(&rx_sync, raw_packet_rx, 4);
            if (rx_sync == SYNC_WORD) {
                rx_cursor = offset;
                locked = 1;
                printf("[RX] Frame locked successfully at symbol offset %u!\n", offset);
                break;
            }
        }
    }

    if (!locked) rx_cursor = 0;

    for (unsigned int c = 0; c < num_chunks; c++) {
        if (rx_cursor + symbols_per_packet > rx_sym_count) break;

        unsigned char enc_packet_rx[enc_msg_len];
        unsigned int temp_cursor = rx_cursor;

        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = 0;
            for (int b = 6; b >= 0; b -= 2) {
                float complex sym_corrected;
                nco_crcf_mix_down(nco_rx, rx_symbols[temp_cursor++], &sym_corrected);
                nco_crcf_step(nco_rx);

                unsigned int pair;
                modem_demodulate(mod_rx, sym_corrected, &pair);

                float complex ideal_sym;
                modem_modulate(mod_rx, pair, &ideal_sym);
                float phase_error = cimagf(conjf(ideal_sym) * sym_corrected);
                nco_crcf_adjust_frequency(nco_rx, 0.0005f * phase_error);
                nco_crcf_adjust_phase(nco_rx,     0.02f   * phase_error);

                bv |= (pair << b);
            }
            enc_packet_rx[i] = bv;
        }

        unsigned char raw_packet_rx[packet_raw_len];
        packetizer p = packetizer_create(packet_raw_len, LIQUID_CRC_32, LIQUID_FEC_HAMMING74, LIQUID_FEC_NONE);
        int crc_pass = packetizer_decode(p, enc_packet_rx, raw_packet_rx);
        packetizer_destroy(p);

        if (crc_pass) {
            unsigned int rx_sync = 0, rx_chunk_idx = 0;
            memcpy(&rx_sync, raw_packet_rx, 4);
            memcpy(&rx_chunk_idx, raw_packet_rx + 4, 4);

            if (rx_sync == SYNC_WORD && rx_chunk_idx < num_chunks) {
                unsigned int current_size = PAYLOAD_CHUNK_SIZE;
                if (rx_chunk_idx == num_chunks - 1) current_size = file_size - (rx_chunk_idx * PAYLOAD_CHUNK_SIZE);
                memcpy(reassembled_file + (rx_chunk_idx * PAYLOAD_CHUNK_SIZE), raw_packet_rx + 8, current_size);
                successful_chunks++;
            }
        }
        rx_cursor += symbols_per_packet;
    }

    modem_destroy(mod_rx);
    nco_crcf_destroy(nco_rx);

    printf("[RX] Decoded %u / %u chunks successfully.\n", successful_chunks, num_chunks);
    if (successful_chunks == num_chunks) {
        printf("[RX] SUCCESS! Streaming architecture verified. Recovered recovered.png\n");
        FILE *fout = fopen("recovered.png", "wb");
        fwrite(reassembled_file, 1, file_size, fout);
        fclose(fout);
    }

    free(file_buffer);
    free(tx_buffer);
    free(rx_symbols);
    free(reassembled_file);
    return 0;
}
