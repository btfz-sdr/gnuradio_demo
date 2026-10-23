#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <complex.h>
#include <math.h>
#include <time.h>
#include <liquid/liquid.h>

#define PAYLOAD_CHUNK_SIZE 128  
#define SYNC_WORD          0xAA55AA55

void apply_channel_impairments(float complex *buffer, unsigned int length, 
                               float cfo, float phase_init, float noise_sigma) 
{
    for (unsigned int i = 0; i < length; i++) {
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
    printf("[TX] Loaded successfully. Size = %ld bytes.\n", file_size);

    unsigned int num_chunks = (file_size + PAYLOAD_CHUNK_SIZE - 1) / PAYLOAD_CHUNK_SIZE;
    printf("[TX] Total file chunks to transmit: %u\n", num_chunks);

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
    
    // 【优化】额外多预留 50 个符号的尾迹空白样值（Flush Tail），防止滤波器末端截断导致最后一个包采样失真
    unsigned int tail_symbols = 50;
    unsigned int total_tx_samples = num_chunks * samples_per_packet + tail_symbols * k + h_len * 2;

    float complex *tx_buffer = (float complex *)calloc(total_tx_samples, sizeof(float complex));
    unsigned int tx_cursor = 0;

    for (unsigned int c = 0; c < num_chunks; c++) {
        unsigned int current_size = PAYLOAD_CHUNK_SIZE;
        if (c == num_chunks - 1) {
            current_size = file_size - (c * PAYLOAD_CHUNK_SIZE);
        }

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

        float complex packet_symbols[symbols_per_packet];
        unsigned int sym_idx = 0;
        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = enc_packet[i];
            for (int b = 6; b >= 0; b -= 2) {
                unsigned int pair = (bv >> b) & 0x03;
                float complex sym;
                modem_modulate(mod_tx, pair, &sym);
                packet_symbols[sym_idx++] = sym;
            }
        }

        for (unsigned int i = 0; i < symbols_per_packet; i++) {
            firinterp_crcf_execute(interpolator, packet_symbols[i], &tx_buffer[tx_cursor]);
            tx_cursor += k;
        }
    }

    // 压入尾迹零符号，让滤波器平稳结束
    for (unsigned int t = 0; t < tail_symbols; t++) {
        firinterp_crcf_execute(interpolator, 0.0f + 0.0f * _Complex_I, &tx_buffer[tx_cursor]);
        tx_cursor += k;
    }

    firinterp_crcf_destroy(interpolator);
    modem_destroy(mod_tx);
    printf("[TX] Generated %u shaped & oversampled RF samples (with flush tail).\n", tx_cursor);

    // 信道损伤模拟
    float sim_cfo = 0.0001f;
    float sim_phase = 0.05f;
    float sim_noise = 0.02f;
    printf("[Channel] Simulating wireless impairments (CFO=%.4f, Phase=%.2f, Noise=%.2f)...\n", 
           sim_cfo, sim_phase, sim_noise);
    apply_channel_impairments(tx_buffer, tx_cursor, sim_cfo, sim_phase, sim_noise);

    // 接收端
    printf("[RX] Starting symbol timing recovery (symsync_crcf)...\n");
    symsync_crcf symsync = symsync_crcf_create_rnyquist(LIQUID_FIRFILT_RRC, k, m, beta, 32);

    unsigned int max_rx_symbols = tx_cursor / k;
    float complex *rx_symbols = (float complex *)malloc(max_rx_symbols * sizeof(float complex));
    unsigned int rx_sym_count = 0;

    for (unsigned int i = 0; i < tx_cursor; i++) {
        float complex y[2];
        unsigned int num_extracted = 0;
        symsync_crcf_execute(symsync, &tx_buffer[i], 1, y, &num_extracted);
        if (num_extracted > 0) {
            for (unsigned int j = 0; j < num_extracted; j++) {
                if (rx_sym_count < max_rx_symbols) {
                    rx_symbols[rx_sym_count++] = y[j];
                }
            }
        }
    }
    symsync_crcf_destroy(symsync);
    printf("[RX] Symbol timing recovery recovered %u symbols. Searching frame lock...\n", rx_sym_count);

    modem mod_rx = modem_create(LIQUID_MODEM_QPSK);
    unsigned int successful_chunks = 0;
    unsigned char *reassembled_file = (unsigned char *)malloc(file_size);
    memset(reassembled_file, 0, file_size);

    int *received_flags = (int *)calloc(num_chunks, sizeof(int));

    int locked = 0;
    unsigned int rx_cursor = 0;
    unsigned int symbols_per_chunk = symbols_per_packet;

    for (unsigned int offset = 0; offset < 200 && offset + symbols_per_chunk <= rx_sym_count; offset++) {
        nco_crcf nco_test = nco_crcf_create(LIQUID_NCO);
        nco_crcf_set_frequency(nco_test, 0.0f);
        nco_crcf_set_phase(nco_test, 0.0f);

        unsigned int temp_cursor = offset;
        unsigned char enc_packet_rx[enc_msg_len];

        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = 0;
            for (int b = 6; b >= 0; b -= 2) {
                float complex sym_raw = rx_symbols[temp_cursor++];
                float complex sym_corrected;
                nco_crcf_mix_down(nco_test, sym_raw, &sym_corrected);
                nco_crcf_step(nco_test);

                unsigned int pair;
                modem_demodulate(mod_rx, sym_corrected, &pair);

                float complex ideal_sym;
                modem_modulate(mod_rx, pair, &ideal_sym);
                float phase_error = cimagf(conjf(ideal_sym) * sym_corrected);
                nco_crcf_adjust_frequency(nco_test, 0.0005f * phase_error);
                nco_crcf_adjust_phase(nco_test,     0.02f   * phase_error);

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

    if (!locked) {
        printf("[RX] ERROR: Failed to lock frame start, defaulting to 0\n");
        rx_cursor = 0;
    }

    nco_crcf nco_rx = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_frequency(nco_rx, 0.0f);
    nco_crcf_set_phase(nco_rx, 0.0f);

    for (unsigned int c = 0; c < num_chunks; c++) {
        if (rx_cursor + symbols_per_chunk > rx_sym_count) break;

        unsigned char enc_packet_rx[enc_msg_len];
        unsigned int temp_cursor = rx_cursor;

        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = 0;
            for (int b = 6; b >= 0; b -= 2) {
                float complex sym_raw = rx_symbols[temp_cursor++];
                float complex sym_corrected;

                nco_crcf_mix_down(nco_rx, sym_raw, &sym_corrected);
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
            unsigned int rx_sync = 0;
            unsigned int rx_chunk_idx = 0;
            memcpy(&rx_sync, raw_packet_rx, 4);
            memcpy(&rx_chunk_idx, raw_packet_rx + 4, 4);

            if (rx_sync == SYNC_WORD && rx_chunk_idx < num_chunks) {
                if (!received_flags[rx_chunk_idx]) {
                    received_flags[rx_chunk_idx] = 1;
                    unsigned int current_size = PAYLOAD_CHUNK_SIZE;
                    if (rx_chunk_idx == num_chunks - 1) {
                        current_size = file_size - (rx_chunk_idx * PAYLOAD_CHUNK_SIZE);
                    }
                    memcpy(reassembled_file + (rx_chunk_idx * PAYLOAD_CHUNK_SIZE), raw_packet_rx + 8, current_size);
                    successful_chunks++;
                }
            }
        }

        rx_cursor += symbols_per_chunk;
    }

    modem_destroy(mod_rx);
    nco_crcf_destroy(nco_rx);

    printf("[RX] Decoded %u / %u chunks successfully.\n", successful_chunks, num_chunks);

    if (successful_chunks == num_chunks) {
        printf("[RX] SUCCESS! 100%% Full RRC + SymSync + Lock-and-Step passed! Recovered recovered.png\n");
        FILE *fout = fopen("recovered.png", "wb");
        if (fout) {
            fwrite(reassembled_file, 1, file_size, fout);
            fclose(fout);
        }
    } else {
        printf("[RX] FAILED: Some chunks corrupted.\n");
    }

    free(file_buffer);
    free(tx_buffer);
    free(rx_symbols);
    free(reassembled_file);
    free(received_flags);

    return 0;
}
