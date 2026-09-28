/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

double utils_get_time_sec(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int initialized = 0;
    if (!initialized) {
        QueryPerformanceFrequency(&freq);
        initialized = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

int64_t utils_get_file_size(const char *path) {
    if (!path) return -1;
#ifdef _WIN32
    struct __stat64 st;
    if (_stat64(path, &st) == 0) {
        return (int64_t)st.st_size;
    }
#else
    struct stat st;
    if (stat(path, &st) == 0) {
        return (int64_t)st.st_size;
    }
#endif
    return -1;
}

void utils_format_bytes(int64_t bytes, char *buf, size_t size) {
    if (!buf || size == 0) return;
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double count = (double)bytes;

    while (count >= 1024.0 && unit_idx < 4) {
        count /= 1024.0;
        unit_idx++;
    }

    if (unit_idx == 0) {
        snprintf(buf, size, "%lld %s", (long long)bytes, units[unit_idx]);
    } else {
        snprintf(buf, size, "%.2f %s", count, units[unit_idx]);
    }
}

void utils_print_progress(int64_t current, int64_t total, int64_t frames, double elapsed_sec) {
    static double last_print_time = 0.0;
    double now = utils_get_time_sec();
    if (now - last_print_time < PROGRESS_INTERVAL_SEC && current < total) {
        return;
    }
    last_print_time = now;

    double percent = 0.0;
    if (total > 0) {
        percent = ((double)current / (double)total) * 100.0;
        if (percent > 100.0) percent = 100.0;
    }

    const int bar_width = 28;
    int filled = (int)((percent / 100.0) * bar_width);
    if (filled > bar_width) filled = bar_width;

    char bar[32];
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) bar[i] = '=';
        else if (i == filled) bar[i] = '>';
        else bar[i] = ' ';
    }
    bar[bar_width] = '\0';

    char cur_str[32], tot_str[32];
    utils_format_bytes(current, cur_str, sizeof(cur_str));
    utils_format_bytes(total, tot_str, sizeof(tot_str));

    double speed_mb = 0.0;
    if (elapsed_sec > 0.001) {
        speed_mb = ((double)current / (1024.0 * 1024.0)) / elapsed_sec;
    }

    int eta_sec = 0;
    if (speed_mb > 0.001 && total > current) {
        double remain_bytes = (double)(total - current);
        eta_sec = (int)(remain_bytes / (speed_mb * 1024.0 * 1024.0));
    }
    int eta_h = eta_sec / 3600;
    int eta_m = (eta_sec % 3600) / 60;
    int eta_s = eta_sec % 60;

    fprintf(stderr, "\r[%s] %5.1f%% (%s / %s) | %lld quadros | %6.1f MB/s | Estimativa: %02d:%02d:%02d  ",
            bar, percent, cur_str, tot_str, (long long)frames, speed_mb, eta_h, eta_m, eta_s);
    fflush(stderr);

    if (current >= total && total > 0) {
        fprintf(stderr, "\n");
    }
}

void utils_print_banner(void) {
    fprintf(stderr, "======================================================================\n");
    fprintf(stderr, "     FixCFTV - Restauração Profissional de Vídeos de CFTV / DVR       \n");
    fprintf(stderr, "                   Desenvolvido por PladixOficial                     \n");
    fprintf(stderr, "              Repositório: https://github.com/pladix/FixCFTV          \n");
    fprintf(stderr, "======================================================================\n\n");
}

const char *utils_codec_name(enum AVCodecID codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_H264: return "H.264 / AVC (Advanced Video Coding)";
        case AV_CODEC_ID_HEVC: return "H.265 / HEVC (High Efficiency Video Coding)";
        case AV_CODEC_ID_MJPEG: return "Motion JPEG";
        case AV_CODEC_ID_MPEG4: return "MPEG-4 Part 2";
        default: return "Codec Genérico de Vídeo";
    }
}

void utils_print_summary(const HealerStats *stats, const HealerConfig *config) {
    double total_time = stats->end_time - stats->start_time;
    if (total_time <= 0.0) total_time = 0.0001;

    char proc_str[32], tot_str[32], drop_str[32], out_str[32];
    utils_format_bytes(stats->processed_bytes, proc_str, sizeof(proc_str));
    utils_format_bytes(stats->total_bytes, tot_str, sizeof(tot_str));
    utils_format_bytes(stats->dropped_bytes, drop_str, sizeof(drop_str));

    int64_t out_size = utils_get_file_size(config->output_path);
    utils_format_bytes(out_size > 0 ? out_size : 0, out_str, sizeof(out_str));

    double speed = ((double)stats->processed_bytes / (1024.0 * 1024.0)) / total_time;
    double fps = (double)stats->recovered_frames / total_time;

    fprintf(stdout, "\n==================== RELATÓRIO FORENSE DE RESTAURAÇÃO ====================\n");
    fprintf(stdout, " Status:                 CONCLUÍDO COM SUCESSO (Vídeo Reconstruído)\n");
    fprintf(stdout, " Método Utilizado:       %s\n", stats->repair_mode_name ? stats->repair_mode_name : "Automático");
    fprintf(stdout, " Arquivo de Entrada:     %s (%s)\n", config->input_path, tot_str);
    fprintf(stdout, " Arquivo Restaurado:     %s (%s)\n", config->output_path, out_str);
    if (config->reference_path[0]) {
        fprintf(stdout, " Arquivo de Referência:  %s\n", config->reference_path);
    }
    fprintf(stdout, " Codec Identificado:     %s\n", utils_codec_name(stats->codec_id));
    fprintf(stdout, " Resolução:              %d x %d pixels\n", stats->width, stats->height);
    if (stats->framerate.den > 0) {
        fprintf(stdout, " Taxa de Quadros (FPS):  %.2f quadros/segundo\n",
                (double)stats->framerate.num / (double)stats->framerate.den);
    }
    fprintf(stdout, " Quadros Recuperados:    %lld (Quadros-Chave IDR: %lld, Interframes: %lld)\n",
            (long long)stats->recovered_frames,
            (long long)stats->keyframes,
            (long long)(stats->recovered_frames - stats->keyframes));
    fprintf(stdout, " Dados Processados:      %s\n", proc_str);
    if (stats->dropped_bytes > 0) {
        fprintf(stdout, " Fragmentos Descartados: %s (bytes nulos / padding de setor)\n", drop_str);
    }
    fprintf(stdout, " Tempo de Processamento: %.2f segundos\n", total_time);
    fprintf(stdout, " Vazão de Leitura:       %.2f MB/s (%.1f quadros/segundo)\n", speed, fps);
    fprintf(stdout, " Otimização do Arquivo:  FastStart (Átomo moov alocado no início para streaming web)\n");
    fprintf(stdout, "=========================================================================\n\n");
}
