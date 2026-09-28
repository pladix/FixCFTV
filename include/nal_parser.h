#ifndef FIXCFTV_NAL_PARSER_H
#define FIXCFTV_NAL_PARSER_H

/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>

// Tipos de Unidades NAL H.264
#define H264_NAL_UNSPECIFIED 0
#define H264_NAL_SLICE       1
#define H264_NAL_DPA         2
#define H264_NAL_DPB         3
#define H264_NAL_DPC         4
#define H264_NAL_IDR_SLICE   5
#define H264_NAL_SEI         6
#define H264_NAL_SPS         7
#define H264_NAL_PPS         8
#define H264_NAL_AUD         9
#define H264_NAL_END_SEQ     10
#define H264_NAL_END_STREAM  11
#define H264_NAL_FILLER      12

// Tipos de Unidades NAL H.265 (HEVC)
#define HEVC_NAL_TRAIL_N     0
#define HEVC_NAL_TRAIL_R     1
#define HEVC_NAL_IDR_W_RADL  19
#define HEVC_NAL_IDR_N_LP    20
#define HEVC_NAL_CRA_NUT     21
#define HEVC_NAL_VPS         32
#define HEVC_NAL_SPS         33
#define HEVC_NAL_PPS         34
#define HEVC_NAL_AUD         35
#define HEVC_NAL_PREFIX_SEI  39
#define HEVC_NAL_SUFFIX_SEI  40

typedef struct {
    int nal_type;
    int is_keyframe;
    int is_param_set;
    size_t offset;
    size_t prefix_len;
    size_t size;
} NalUnitInfo;

/**
 * Estrutura de Perícia Forense Automática.
 * Mapeia com precisão cirúrgica a estrutura interna do arquivo danificado.
 */
typedef struct {
    int64_t file_size;
    int is_mp4_container;
    int has_mdat;
    int has_moov;
    int64_t mdat_offset;
    int64_t mdat_size;
    int is_dhav;             // Formato Dahua/Intelbras DHAV (.dav ou gravado bruto)
    int is_mpegts;           // Stream de transporte MPEG-TS (Sync byte 0x47 a cada 188B)
    int is_avcc;             // Prefixo de 4 bytes big-endian de tamanho
    int is_annexb;           // Start codes 00 00 01 ou 00 00 00 01
    enum AVCodecID detected_codec; // H.264 ou H.265
    int has_sps;             // Parâmetro SPS encontrado no próprio fluxo
    int has_pps;             // Parâmetro PPS encontrado no próprio fluxo
    int has_vps;             // Parâmetro VPS encontrado (H.265)
    int width;               // Resolução decodificada do bitstream
    int height;              // Altura decodificada do bitstream
    int idr_count;           // Quadros-chave encontrados na varredura inicial
    int slice_count;         // Total de fatias de vídeo identificadas
    const char *detected_issue; // Causa provável (Queda de energia, desligamento abrupto, etc.)
} ForensicProbe;

/**
 * Localiza o próximo start code Annex B (00 00 01 ou 00 00 00 01) no buffer.
 */
const uint8_t *nal_find_start_code(const uint8_t *buf, size_t size, size_t *prefix_len);

/**
 * Analisa o cabeçalho NAL de streams H.264.
 */
void nal_parse_h264_header(uint8_t byte, int *nal_type, int *is_keyframe, int *is_param_set);

/**
 * Analisa o cabeçalho NAL de streams H.265 / HEVC.
 */
void nal_parse_hevc_header(uint8_t byte1, int *nal_type, int *is_keyframe, int *is_param_set);

/**
 * Decodifica o bitstream SPS H.264 via Exponential-Golomb para obter resolução exata (largura e altura).
 */
int nal_parse_h264_sps(const uint8_t *sps_buf, size_t sps_size, int *width, int *height);

/**
 * Identifica automaticamente se o fluxo de vídeo gravado é H.264 ou H.265.
 */
enum AVCodecID nal_probe_codec(const uint8_t *buf, size_t size);

/**
 * Localiza o início dos dados brutos de vídeo (átomo mdat) dentro da estrutura do contêiner.
 */
int nal_locate_mdat(FILE *fp, int64_t file_size, int64_t *data_offset, int64_t *data_size);

/**
 * Verifica se os dados correspondem a pacotes DHAV da Dahua / Intelbras.
 */
int nal_is_dhav(const uint8_t *buf, size_t size);

/**
 * Verifica se os dados correspondem a um fluxo de transporte MPEG-TS.
 */
int nal_is_mpegts(const uint8_t *buf, size_t size);

/**
 * Executa uma análise forense completa do arquivo danificado.
 * Preenche a estrutura ForensicProbe e formata um relatório textual amigável e técnico.
 */
int nal_forensic_probe(const char *file_path, ForensicProbe *probe, char *diag_msg, size_t diag_msg_size);

/**
 * Gera parâmetros sintéticos SPS e PPS válidos para H.264 quando os parâmetros originais
 * foram perdidos e a câmera não os emitiu no meio do fluxo.
 */
int nal_generate_synthetic_sps_pps(int width, int height, int fps, uint8_t **out_buf, int *out_size);

#endif // FIXCFTV_NAL_PARSER_H
