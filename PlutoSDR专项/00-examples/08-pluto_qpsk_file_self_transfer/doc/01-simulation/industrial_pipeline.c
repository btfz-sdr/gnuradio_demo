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

    unsigned int symbols_per_packet = (enc_msg_len * 8) / 2;
    unsigned int total_tx_symbols = num_chunks * symbols_per_packet;

    float complex *tx_symbols = (float complex *)malloc(total_tx_symbols * sizeof(float complex));
    unsigned int sym_cursor = 0;

    modem mod_tx = modem_create(LIQUID_MODEM_QPSK);

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

        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = enc_packet[i];
            for (int b = 6; b >= 0; b -= 2) {
                unsigned int pair = (bv >> b) & 0x03;
                float complex sym;
                modem_modulate(mod_tx, pair, &sym);
                tx_symbols[sym_cursor++] = sym;
            }
        }
    }
    modem_destroy(mod_tx);
    printf("[TX] Generated %u pure QPSK symbols.\n", sym_cursor);

    // 施加真实的信道损伤（包含频偏与噪声）
    float sim_cfo = 0.0001f;
    float sim_phase = 0.05f;
    float sim_noise = 0.02f;
    printf("[Channel] Simulating channel impairments (CFO=%.4f, Phase=%.2f, Noise=%.2f)...\n", 
           sim_cfo, sim_phase, sim_noise);
    apply_channel_impairments(tx_symbols, sym_cursor, sim_cfo, sim_phase, sim_noise);

    // ==========================================
    // 接收端：引入实时 NCO 载波跟踪环路
    // ==========================================
    printf("[RX] Decoding packets with real-time NCO carrier tracking...\n");
    modem mod_rx = modem_create(LIQUID_MODEM_QPSK);
    
    // 创建数值控制振荡器（NCO）用于动态消除频偏与相偏
    nco_crcf nco_rx = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_frequency(nco_rx, 0.0f);
    nco_crcf_set_phase(nco_rx, 0.0f);

    unsigned int successful_chunks = 0;
    unsigned char *reassembled_file = (unsigned char *)malloc(file_size);

    unsigned int rx_cursor = 0;
    for (unsigned int c = 0; c < num_chunks; c++) {
        if (rx_cursor + symbols_per_packet > sym_cursor) break;

        unsigned char enc_packet_rx[enc_msg_len];
        for (unsigned int i = 0; i < enc_msg_len; i++) {
            unsigned char bv = 0;
            for (int b = 6; b >= 0; b -= 2) {
                float complex sym_raw = tx_symbols[rx_cursor++];
                float complex sym_corrected;

                // 1. 实时混频下变频纠偏
                nco_crcf_mix_down(nco_rx, sym_raw, &sym_corrected);
                nco_crcf_step(nco_rx);

                // 2. 解调并计算判决引导误差，实时更新环路
                unsigned int pair;
                modem_demodulate(mod_rx, sym_corrected, &pair);

                float complex ideal_sym;
                modem_modulate(mod_rx, pair, &ideal_sym);

                float phase_error = cimagf(conjf(ideal_sym) * sym_corrected);
                nco_crcf_adjust_frequency(nco_rx, 0.001f * phase_error);
                nco_crcf_adjust_phase(nco_rx,     0.05f  * phase_error);

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
                unsigned int current_size = PAYLOAD_CHUNK_SIZE;
                if (rx_chunk_idx == num_chunks - 1) {
                    current_size = file_size - (rx_chunk_idx * PAYLOAD_CHUNK_SIZE);
                }
                memcpy(reassembled_file + (rx_chunk_idx * PAYLOAD_CHUNK_SIZE), raw_packet_rx + 8, current_size);
                successful_chunks++;
            }
        }
    }

    modem_destroy(mod_rx);
    nco_crcf_destroy(nco_rx);

    printf("[RX] Decoded %u / %u chunks successfully.\n", successful_chunks, num_chunks);

    if (successful_chunks == num_chunks) {
        printf("[RX] SUCCESS! File successfully recovered as recovered.png!\n");
        FILE *fout = fopen("recovered.png", "wb");
        if (fout) {
            fwrite(reassembled_file, 1, file_size, fout);
            fclose(fout);
        }
    } else {
        printf("[RX] FAILED: Some corrupted chunks.\n");
    }

    free(file_buffer);
    free(tx_symbols);
    free(reassembled_file);

    return 0;
}
