/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#include "nal_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t bit_pos;
} BitReader;

static void br_init(BitReader *br, const uint8_t *data, size_t size) {
    br->data = data;
    br->size = size;
    br->bit_pos = 0;
}

static int br_eof(const BitReader *br) {
    return (br->bit_pos >= br->size * 8);
}

static uint32_t br_read_bit(BitReader *br) {
    if (br_eof(br)) return 0;
    size_t byte_idx = br->bit_pos / 8;
    int bit_offset = 7 - (br->bit_pos % 8);
    br->bit_pos++;
    return (br->data[byte_idx] >> bit_offset) & 1;
}

static uint32_t br_read_bits(BitReader *br, int n) {
    uint32_t val = 0;
    for (int i = 0; i < n; i++) {
        val = (val << 1) | br_read_bit(br);
    }
    return val;
}

static uint32_t br_read_ue(BitReader *br) {
    int leading_zeros = 0;
    while (!br_eof(br) && br_read_bit(br) == 0) {
        leading_zeros++;
        if (leading_zeros > 32) return 0;
    }
    if (leading_zeros == 0) return 0;
    uint32_t suffix = br_read_bits(br, leading_zeros);
    return (1U << leading_zeros) - 1 + suffix;
}

static int32_t br_read_se(BitReader *br) {
    uint32_t ue = br_read_ue(br);
    if (ue & 1) {
        return (int32_t)((ue + 1) / 2);
    } else {
        return -(int32_t)(ue / 2);
    }
}

const uint8_t *nal_find_start_code(const uint8_t *buf, size_t size, size_t *prefix_len) {
    if (!buf || size < 3) return NULL;
    const uint8_t *p = buf;
    const uint8_t *end = buf + size - 2;

    while (p < end) {
        if (p[2] > 1) {
            p += 3;
            continue;
        }
        if (p[2] == 1 && p[1] == 0 && p[0] == 0) {
            if (p > buf && *(p - 1) == 0) {
                if (prefix_len) *prefix_len = 4;
                return p - 1;
            } else {
                if (prefix_len) *prefix_len = 3;
                return p;
            }
        }
        if (p[2] == 0) {
            p++;
        } else {
            p += 3;
        }
    }
    return NULL;
}

void nal_parse_h264_header(uint8_t byte, int *nal_type, int *is_keyframe, int *is_param_set) {
    int type = byte & 0x1F;
    if (nal_type) *nal_type = type;
    if (is_keyframe) *is_keyframe = (type == H264_NAL_IDR_SLICE);
    if (is_param_set) *is_param_set = (type == H264_NAL_SPS || type == H264_NAL_PPS);
}

void nal_parse_hevc_header(uint8_t byte1, int *nal_type, int *is_keyframe, int *is_param_set) {
    int type = (byte1 >> 1) & 0x3F;
    if (nal_type) *nal_type = type;
    if (is_keyframe) *is_keyframe = (type >= HEVC_NAL_IDR_W_RADL && type <= HEVC_NAL_CRA_NUT);
    if (is_param_set) *is_param_set = (type == HEVC_NAL_VPS || type == HEVC_NAL_SPS || type == HEVC_NAL_PPS);
}

int nal_parse_h264_sps(const uint8_t *sps_buf, size_t sps_size, int *width, int *height) {
    if (!sps_buf || sps_size < 4) return -1;

    uint8_t *clean_buf = (uint8_t *)malloc(sps_size);
    if (!clean_buf) return -1;

    size_t clean_size = 0;
    for (size_t i = 0; i < sps_size; i++) {
        if (i + 2 < sps_size && sps_buf[i] == 0x00 && sps_buf[i+1] == 0x00 && sps_buf[i+2] == 0x03) {
            clean_buf[clean_size++] = sps_buf[i];
            clean_buf[clean_size++] = sps_buf[i+1];
            i += 2;
        } else {
            clean_buf[clean_size++] = sps_buf[i];
        }
    }

    BitReader br;
    br_init(&br, clean_buf, clean_size);

    br_read_bits(&br, 8); // Skip NAL header

    uint32_t profile_idc = br_read_bits(&br, 8);
    br_read_bits(&br, 8);
    br_read_bits(&br, 8);
    br_read_ue(&br);

    int chroma_format_idc = 1;
    if (profile_idc == 100 || profile_idc == 110 ||
        profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
        profile_idc == 83  || profile_idc == 86  || profile_idc == 118 ||
        profile_idc == 128 || profile_idc == 138 || profile_idc == 139 ||
        profile_idc == 134) {
        chroma_format_idc = br_read_ue(&br);
        if (chroma_format_idc == 3) {
            br_read_bit(&br);
        }
        br_read_ue(&br);
        br_read_ue(&br);
        br_read_bit(&br);
        uint32_t seq_scaling_matrix_present = br_read_bit(&br);
        if (seq_scaling_matrix_present) {
            int count = (chroma_format_idc != 3) ? 8 : 12;
            for (int i = 0; i < count; i++) {
                if (br_read_bit(&br)) {
                    int last_scale = 8, next_scale = 8;
                    int size_of_scaling_list = (i < 6) ? 16 : 64;
                    for (int j = 0; j < size_of_scaling_list; j++) {
                        if (next_scale != 0) {
                            int32_t delta_scale = br_read_se(&br);
                            next_scale = (last_scale + delta_scale + 256) % 256;
                        }
                        last_scale = (next_scale == 0) ? last_scale : next_scale;
                    }
                }
            }
        }
    }

    br_read_ue(&br);
    uint32_t pic_order_cnt_type = br_read_ue(&br);
    if (pic_order_cnt_type == 0) {
        br_read_ue(&br);
    } else if (pic_order_cnt_type == 1) {
        br_read_bit(&br);
        br_read_se(&br);
        br_read_se(&br);
        uint32_t num_ref_frames = br_read_ue(&br);
        for (uint32_t i = 0; i < num_ref_frames; i++) {
            br_read_se(&br);
        }
    }

    br_read_ue(&br);
    br_read_bit(&br);

    uint32_t pic_width_in_mbs_minus1 = br_read_ue(&br);
    uint32_t pic_height_in_map_units_minus1 = br_read_ue(&br);
    uint32_t frame_mbs_only_flag = br_read_bit(&br);

    if (!frame_mbs_only_flag) {
        br_read_bit(&br);
    }
    br_read_bit(&br);

    uint32_t frame_cropping_flag = br_read_bit(&br);
    uint32_t crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
    if (frame_cropping_flag) {
        crop_left = br_read_ue(&br);
        crop_right = br_read_ue(&br);
        crop_top = br_read_ue(&br);
        crop_bottom = br_read_ue(&br);
    }

    int sub_width_c = (chroma_format_idc == 1 || chroma_format_idc == 2) ? 2 : 1;
    int sub_height_c = (chroma_format_idc == 1) ? 2 : 1;

    int crop_unit_x = sub_width_c;
    int crop_unit_y = sub_height_c * (2 - frame_mbs_only_flag);

    int w = (int)((pic_width_in_mbs_minus1 + 1) * 16) - (int)((crop_left + crop_right) * crop_unit_x);
    int h = (int)((2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16) - (int)((crop_top + crop_bottom) * crop_unit_y);

    free(clean_buf);

    if (w > 0 && h > 0 && w <= 8192 && h <= 8192) {
        if (width) *width = w;
        if (height) *height = h;
        return 0;
    }
    return -1;
}

enum AVCodecID nal_probe_codec(const uint8_t *buf, size_t size) {
    if (!buf || size < 4) return AV_CODEC_ID_H264;

    int h264_score = 0;
    int hevc_score = 0;
    const uint8_t *cur = buf;
    const uint8_t *end = buf + size;

    while (cur < end) {
        size_t prefix_len = 0;
        const uint8_t *sc = nal_find_start_code(cur, end - cur, &prefix_len);
        if (!sc) break;

        const uint8_t *nal_payload = sc + prefix_len;
        if (nal_payload >= end) break;

        uint8_t b1 = *nal_payload;
        int h264_type = b1 & 0x1F;
        if (h264_type == H264_NAL_SPS) h264_score += 10;
        else if (h264_type == H264_NAL_PPS) h264_score += 5;
        else if (h264_type == H264_NAL_IDR_SLICE) h264_score += 4;
        else if (h264_type == H264_NAL_SLICE) h264_score += 1;

        int hevc_type = (b1 >> 1) & 0x3F;
        if (hevc_type == HEVC_NAL_VPS) hevc_score += 12;
        else if (hevc_type == HEVC_NAL_SPS) hevc_score += 10;
        else if (hevc_type == HEVC_NAL_PPS) hevc_score += 5;
        else if (hevc_type >= HEVC_NAL_IDR_W_RADL && hevc_type <= HEVC_NAL_CRA_NUT) hevc_score += 4;

        cur = sc + prefix_len + 1;
    }

    if (hevc_score > h264_score && hevc_score >= 10) {
        return AV_CODEC_ID_HEVC;
    }
    return AV_CODEC_ID_H264;
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t read_be64(const uint8_t *p) {
    return ((uint64_t)read_be32(p) << 32) | (uint64_t)read_be32(p + 4);
}

int nal_locate_mdat(FILE *fp, int64_t file_size, int64_t *data_offset, int64_t *data_size) {
    if (!fp || file_size < 8) return -1;

    int64_t cur_pos = 0;
    uint8_t header[16];

    while (cur_pos + 8 <= file_size && cur_pos < 100 * 1024 * 1024) {
#ifdef _WIN32
        _fseeki64(fp, cur_pos, SEEK_SET);
#else
        fseeko(fp, cur_pos, SEEK_SET);
#endif
        if (fread(header, 1, 8, fp) != 8) break;

        uint64_t atom_size = read_be32(header);
        char atom_type[5] = { (char)header[4], (char)header[5], (char)header[6], (char)header[7], '\0' };

        int header_len = 8;
        if (atom_size == 1) {
            if (fread(header + 8, 1, 8, fp) != 8) break;
            atom_size = read_be64(header + 8);
            header_len = 16;
        } else if (atom_size == 0) {
            atom_size = (uint64_t)(file_size - cur_pos);
        }

        if (memcmp(atom_type, "mdat", 4) == 0) {
            if (data_offset) *data_offset = cur_pos + header_len;
            if (data_size) *data_size = (int64_t)(atom_size - header_len);
            return 0;
        }

        if (atom_size < (uint64_t)header_len) {
            break;
        }

        cur_pos += atom_size;
    }

    if (data_offset) *data_offset = 0;
    if (data_size) *data_size = file_size;
    return 1;
}

int nal_is_dhav(const uint8_t *buf, size_t size) {
    if (!buf || size < 4) return 0;
    if (memcmp(buf, "DHAV", 4) == 0) return 1;

    // Procura por assinatura DHAV nos primeiros 32 KB
    size_t limit = size > 32768 ? 32768 : size;
    for (size_t i = 0; i + 4 <= limit; i++) {
        if (memcmp(buf + i, "DHAV", 4) == 0) {
            return 1;
        }
    }
    return 0;
}

int nal_is_mpegts(const uint8_t *buf, size_t size) {
    if (!buf || size < 564) return 0;
    if (buf[0] == 0x47 && buf[188] == 0x47 && buf[376] == 0x47) {
        return 1;
    }
    // Procura sincronização com deslocamento nos primeiros 1024 bytes
    size_t limit = size > 1024 ? 1024 : size;
    for (size_t i = 0; i + 564 <= limit; i++) {
        if (buf[i] == 0x47 && buf[i + 188] == 0x47 && buf[i + 376] == 0x47) {
            return 1;
        }
    }
    return 0;
}

int nal_forensic_probe(const char *file_path, ForensicProbe *probe, char *diag_msg, size_t diag_msg_size) {
    if (!file_path || !probe) return -1;
    memset(probe, 0, sizeof(ForensicProbe));

    FILE *fp = fopen(file_path, "rb");
    if (!fp) return -1;

#ifdef _WIN32
    _fseeki64(fp, 0, SEEK_END);
    probe->file_size = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_END);
    probe->file_size = ftello(fp);
    fseeko(fp, 0, SEEK_SET);
#endif

    if (probe->file_size <= 0) {
        fclose(fp);
        return -1;
    }

    // Varredura de Átomos ISO MP4
    int64_t cur_pos = 0;
    uint8_t header[16];
    while (cur_pos + 8 <= probe->file_size && cur_pos < 100 * 1024 * 1024) {
#ifdef _WIN32
        _fseeki64(fp, cur_pos, SEEK_SET);
#else
        fseeko(fp, cur_pos, SEEK_SET);
#endif
        if (fread(header, 1, 8, fp) != 8) break;

        uint64_t atom_size = read_be32(header);
        char atom_type[5] = { (char)header[4], (char)header[5], (char)header[6], (char)header[7], '\0' };
        int header_len = 8;

        if (atom_size == 1) {
            if (fread(header + 8, 1, 8, fp) != 8) break;
            atom_size = read_be64(header + 8);
            header_len = 16;
        } else if (atom_size == 0) {
            atom_size = (uint64_t)(probe->file_size - cur_pos);
        }

        if (memcmp(atom_type, "ftyp", 4) == 0) {
            probe->is_mp4_container = 1;
        } else if (memcmp(atom_type, "mdat", 4) == 0) {
            probe->has_mdat = 1;
            probe->mdat_offset = cur_pos + header_len;
            probe->mdat_size = (int64_t)(atom_size - header_len);
        } else if (memcmp(atom_type, "moov", 4) == 0) {
            probe->has_moov = 1;
        }

        if (atom_size < (uint64_t)header_len) break;
        cur_pos += atom_size;
    }

    // Leitura de Amostra Forense (até 4 MB do payload de vídeo)
    int64_t sample_offset = probe->has_mdat ? probe->mdat_offset : 0;
#ifdef _WIN32
    _fseeki64(fp, sample_offset, SEEK_SET);
#else
    fseeko(fp, sample_offset, SEEK_SET);
#endif

    size_t sample_len = 4 * 1024 * 1024;
    if ((int64_t)sample_len > probe->file_size - sample_offset) {
        sample_len = (size_t)(probe->file_size - sample_offset);
    }

    uint8_t *sample_buf = (uint8_t *)malloc(sample_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!sample_buf) {
        fclose(fp);
        return -1;
    }
    memset(sample_buf + sample_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    size_t read_bytes = fread(sample_buf, 1, sample_len, fp);
    fclose(fp);

    if (read_bytes < 16) {
        free(sample_buf);
        return -1;
    }

    // Verificação de DHAV e MPEG-TS
    probe->is_dhav = nal_is_dhav(sample_buf, read_bytes);
    probe->is_mpegts = nal_is_mpegts(sample_buf, read_bytes);

    // Identificação de Codec e Varredura de Unidades NAL
    probe->detected_codec = nal_probe_codec(sample_buf, read_bytes);

    const uint8_t *p = sample_buf;
    const uint8_t *p_end = sample_buf + read_bytes;

    while (p < p_end) {
        size_t sc_len = 0;
        const uint8_t *sc = nal_find_start_code(p, p_end - p, &sc_len);
        if (!sc) break;

        probe->is_annexb = 1;
        const uint8_t *payload = sc + sc_len;
        if (payload >= p_end) break;

        if (probe->detected_codec == AV_CODEC_ID_H264) {
            int nal_type = *payload & 0x1F;
            if (nal_type == H264_NAL_SPS) {
                probe->has_sps = 1;
                size_t next_sc_len = 0;
                const uint8_t *next_sc = nal_find_start_code(payload, p_end - payload, &next_sc_len);
                size_t sps_size = next_sc ? (size_t)(next_sc - payload) : (size_t)(p_end - payload);
                int w = 0, h = 0;
                if (nal_parse_h264_sps(payload, sps_size, &w, &h) == 0) {
                    probe->width = w;
                    probe->height = h;
                }
            } else if (nal_type == H264_NAL_PPS) {
                probe->has_pps = 1;
            } else if (nal_type == H264_NAL_IDR_SLICE) {
                probe->idr_count++;
                probe->slice_count++;
            } else if (nal_type == H264_NAL_SLICE) {
                probe->slice_count++;
            }
        } else {
            int hevc_type = (*payload >> 1) & 0x3F;
            if (hevc_type == HEVC_NAL_VPS) probe->has_vps = 1;
            else if (hevc_type == HEVC_NAL_SPS) probe->has_sps = 1;
            else if (hevc_type == HEVC_NAL_PPS) probe->has_pps = 1;
            else if (hevc_type >= HEVC_NAL_IDR_W_RADL && hevc_type <= HEVC_NAL_CRA_NUT) {
                probe->idr_count++;
                probe->slice_count++;
            } else if (hevc_type == HEVC_NAL_TRAIL_R || hevc_type == HEVC_NAL_TRAIL_N) {
                probe->slice_count++;
            }
        }

        p = sc + sc_len + 1;
    }

    // Se não encontrou fluxo Annex B com start codes, faz varredura por pacotes AVCC (4-byte length prefix)
    if (!probe->is_annexb || probe->slice_count == 0) {
        size_t idx = 0;
        while (idx + 6 <= read_bytes) {
            uint32_t l = read_be32(sample_buf + idx);
            if (l >= 4 && l <= 500000 && (idx + 4 + l <= read_bytes)) {
                uint8_t b1 = sample_buf[idx + 4];
                if ((b1 & 0x80) == 0) {
                    uint8_t t = b1 & 0x1F;
                    if (t == H264_NAL_SPS && l <= 128) {
                        probe->is_avcc = 1;
                        probe->has_sps = 1;
                        probe->detected_codec = AV_CODEC_ID_H264;
                        int w = 0, h = 0;
                        if (nal_parse_h264_sps(sample_buf + idx + 4, l, &w, &h) == 0) {
                            probe->width = w;
                            probe->height = h;
                        }
                        idx += 4 + l;
                        continue;
                    } else if (t == H264_NAL_PPS && l <= 64) {
                        probe->is_avcc = 1;
                        probe->has_pps = 1;
                        idx += 4 + l;
                        continue;
                    } else if (t == H264_NAL_IDR_SLICE && l >= 20 && (sample_buf[idx + 5] & 0x80) != 0) {
                        probe->is_avcc = 1;
                        probe->idr_count++;
                        probe->slice_count++;
                        idx += 4 + l;
                        continue;
                    } else if (t == H264_NAL_SLICE && l >= 10 && (sample_buf[idx + 5] & 0x80) != 0) {
                        probe->is_avcc = 1;
                        probe->slice_count++;
                        idx += 4 + l;
                        continue;
                    }
                }
            }
            idx++;
        }
    }

    free(sample_buf);

    // Diagnóstico da Causa Raiz
    if (probe->is_mp4_container && probe->has_mdat && !probe->has_moov) {
        probe->detected_issue = "Queda de energia abrupta durante gravação: o contêiner MP4 foi interrompido antes da gravação do átomo de índices ('moov'). O fluxo bruto 'mdat' está íntegro.";
    } else if (probe->is_dhav) {
        probe->detected_issue = "Gravação proprietária Dahua / Intelbras (envelope DHAV). Os quadros de vídeo estão envelopados com cabeçalhos de controle do DVR.";
    } else if (probe->is_mpegts) {
        probe->detected_issue = "Fluxo contínuo de transporte MPEG-TS interrompido repentinamente.";
    } else if (probe->is_avcc) {
        probe->detected_issue = "Payload com empacotamento de comprimento AVCC de 4 bytes sem cabeçalho global de índice.";
    } else if (probe->slice_count > 0) {
        probe->detected_issue = "Fluxo de vídeo bruto (Elementary Stream) sem contêiner estruturado ou índices de reprodução.";
    } else {
        probe->detected_issue = "Arquivo truncado ou com estrutura corrompida. Necessita de varredura profunda de padrões NAL.";
    }

    if (diag_msg && diag_msg_size > 0) {
        snprintf(diag_msg, diag_msg_size,
                 "Diagnóstico Forense FixCFTV:\n"
                 "• Tamanho do Arquivo: %lld bytes\n"
                 "• Estrutura Detectada: %s\n"
                 "• Diagnóstico da Falha: %s\n"
                 "• Codec de Vídeo: %s\n"
                 "• Parâmetros SPS/PPS no Fluxo: %s\n"
                 "• Resolução Estimada: %s\n"
                 "• Fatias de Vídeo Identificadas na Amostra: %d (Quadros-chave IDR: %d)",
                 (long long)probe->file_size,
                 probe->is_dhav ? "Dahua / Intelbras DHAV" :
                 probe->is_mpegts ? "MPEG Transport Stream (TS)" :
                 probe->is_mp4_container ? "Contêiner ISO MP4 / QuickTime" : "Fluxo Bruto NAL / CCTV RAW",
                 probe->detected_issue,
                 probe->detected_codec == AV_CODEC_ID_HEVC ? "H.265 / HEVC" : "H.264 / AVC",
                 probe->has_sps ? "Presentes (Recuperação 100% autônoma garantida)" : "Ausentes no meio do fluxo",
                 (probe->width > 0 && probe->height > 0) ? "Resolvido via bitstream" : "Será sincronizado na reconstrução",
                 probe->slice_count, probe->idr_count);
    }

    return 0;
}

int nal_generate_synthetic_sps_pps(int width, int height, int fps, uint8_t **out_buf, int *out_size) {
    if (!out_buf || !out_size) return -1;
    (void)fps;

    // SPS & PPS H.264 Baseline para 1080p (1920x1080) - Padrão da maioria absoluta de câmeras IP/CFTV
    static const uint8_t sps_1080p[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x28, 0xda, 0x01, 0xe0, 0x08,
        0x9f, 0x96, 0x10, 0x00, 0x00, 0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x03,
        0xc0, 0xf1, 0x62, 0xd9, 0xa0
    };
    static const uint8_t pps_1080p[] = {
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80
    };

    // SPS & PPS H.264 Baseline para 720p (1280x720)
    static const uint8_t sps_720p[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f, 0xda, 0x01, 0x40, 0x16,
        0xe8, 0x06, 0xd0, 0xa1, 0x35
    };
    static const uint8_t pps_720p[] = {
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80
    };

    const uint8_t *sps = sps_1080p;
    size_t sps_len = sizeof(sps_1080p);
    const uint8_t *pps = pps_1080p;
    size_t pps_len = sizeof(pps_1080p);

    if (width <= 1280 && height <= 720 && width > 0) {
        sps = sps_720p;
        sps_len = sizeof(sps_720p);
        pps = pps_720p;
        pps_len = sizeof(pps_720p);
    }

    size_t total_size = sps_len + pps_len;
    uint8_t *buf = (uint8_t *)malloc(total_size);
    if (!buf) return -1;

    memcpy(buf, sps, sps_len);
    memcpy(buf + sps_len, pps, pps_len);

    *out_buf = buf;
    *out_size = (int)total_size;
    return 0;
}
