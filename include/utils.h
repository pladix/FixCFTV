#ifndef FIXCFTV_UTILS_H
#define FIXCFTV_UTILS_H

/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define CHUNK_BUFFER_SIZE (1024 * 1024) // Buffer de streaming de 1 MB
#define PROGRESS_INTERVAL_SEC 0.15     // Intervalo de atualização da barra de progresso

typedef struct {
    char input_path[1024];
    char output_path[1024];
    char reference_path[1024];
    char batch_dir[1024];
    char batch_out_dir[1024];
    int force_raw_scan;
    int verbose;
    int default_fps;
    int batch_mode;
} HealerConfig;

typedef struct {
    int64_t total_bytes;
    int64_t processed_bytes;
    int64_t recovered_frames;
    int64_t keyframes;
    int64_t dropped_bytes;
    double start_time;
    double end_time;
    int width;
    int height;
    enum AVCodecID codec_id;
    AVRational framerate;
    const char *repair_mode_name;
    int needs_reference;
    char forensic_summary[1024];
} HealerStats;

/**
 * Retorna o tempo decorrido em segundos de alta resolução (monotonic timer).
 */
double utils_get_time_sec(void);

/**
 * Retorna o tamanho total do arquivo em bytes, ou -1 em caso de erro.
 */
int64_t utils_get_file_size(const char *path);

/**
 * Converte bytes para formato legível (B, KB, MB, GB, TB).
 */
void utils_format_bytes(int64_t bytes, char *buf, size_t size);

/**
 * Exibe a barra dinâmica de progresso no terminal com vazão em MB/s e estimativa de término.
 */
void utils_print_progress(int64_t current, int64_t total, int64_t frames, double elapsed_sec);

/**
 * Exibe o cabeçalho oficial do FixCFTV no terminal.
 */
void utils_print_banner(void);

/**
 * Emite o relatório de diagnóstico forense após a restauração.
 */
void utils_print_summary(const HealerStats *stats, const HealerConfig *config);

/**
 * Retorna o nome amigável do codec de vídeo identificado.
 */
const char *utils_codec_name(enum AVCodecID codec_id);

#endif // FIXCFTV_UTILS_H
