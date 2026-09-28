/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "healer.h"
#include "utils.h"

typedef struct {
    char filename[260];
    int64_t file_size;
    int success;
    int needs_ref;
    int64_t frames;
    int width;
    int height;
    const char *method;
    double elapsed;
} BatchFileRecord;

static void print_usage(const char *prog_name) {
    fprintf(stderr, "Uso Individual: %s -i <corrompido.mp4> -o <recuperado.mp4> [opções]\n", prog_name);
    fprintf(stderr, "Uso em Massa:   %s -b <pasta_com_videos> -o <pasta_destino> [opções]\n\n", prog_name);
    fprintf(stderr, "Opções de Entrada:\n");
    fprintf(stderr, "  -i <arquivo>         Arquivo corrompido individual (DVR, NVR, CFTV, MP4/MOV/DAT/DAV).\n");
    fprintf(stderr, "  -o <arquivo/pasta>   Caminho do novo arquivo MP4 recuperado ou pasta de saída no modo lote.\n");
    fprintf(stderr, "  -b, --batch <pasta>  Processa automaticamente todos os vídeos de uma pasta em massa (sem limite).\n\n");
    fprintf(stderr, "Opções de Recuperação:\n");
    fprintf(stderr, "  -r <arquivo>         Vídeo íntegro de referência gravado pelo mesmo DVR/canal (para transplante).\n");
    fprintf(stderr, "  -f                   Força o Método 2 (Varredura bruta NAL direta).\n");
    fprintf(stderr, "  --fps <n>            Define a taxa de quadros padrão caso não identificada (Padrão: 25).\n");
    fprintf(stderr, "  -v, --verbose        Exibe logs forenses detalhados e diagnósticos de baixo nível.\n");
    fprintf(stderr, "  -h, --help           Exibe esta tela de ajuda.\n\n");
    fprintf(stderr, "Exemplos práticos:\n");
    fprintf(stderr, "  %s -i queda_luz.mp4 -o camera_recuperada.mp4\n", prog_name);
    fprintf(stderr, "  %s -b D:\\Gravacoes_DVR -o D:\\Videos_Recuperados\n", prog_name);
    fprintf(stderr, "  %s -i disco_dvr.dat -o camera_recuperada.mp4 -r camera_ontem.mp4\n\n", prog_name);
    fprintf(stderr, "Projeto no GitHub: https://github.com/pladix/FixCFTV\n");
    fprintf(stderr, "Desenvolvido por PladixOficial\n");
}

static int is_supported_video_ext(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    if (_stricmp(ext, ".mp4") == 0 ||
        _stricmp(ext, ".mov") == 0 ||
        _stricmp(ext, ".dat") == 0 ||
        _stricmp(ext, ".dav") == 0 ||
        _stricmp(ext, ".264") == 0 ||
        _stricmp(ext, ".h264") == 0 ||
        _stricmp(ext, ".ts") == 0 ||
        _stricmp(ext, ".raw") == 0 ||
        _stricmp(ext, ".avi") == 0) {
        // Ignora arquivos já recuperados
        if (strstr(name, "_recuperado.mp4") != NULL) return 0;
        return 1;
    }
    return 0;
}

static int run_batch_mode(const HealerConfig *base_config) {
    fprintf(stdout, "[*] MODO EM MASSA ATIVADO: Escaneando diretório '%s'...\n", base_config->batch_dir);

#ifdef _WIN32
    CreateDirectoryA(base_config->batch_out_dir, NULL);
    char search_pattern[2048];
    snprintf(search_pattern, sizeof(search_pattern), "%.1000s\\*.*", base_config->batch_dir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[-] Erro: Não foi possível abrir o diretório '%s'.\n", base_config->batch_dir);
        return 1;
    }

    size_t capacity = 64;
    size_t count = 0;
    char (*files)[512] = malloc(capacity * sizeof(*files));
    if (!files) {
        FindClose(hFind);
        return 1;
    }

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (is_supported_video_ext(fd.cFileName)) {
                if (count >= capacity) {
                    capacity *= 2;
                    char (*new_files)[512] = realloc(files, capacity * sizeof(*files));
                    if (!new_files) break;
                    files = new_files;
                }
                snprintf(files[count++], 512, "%s", fd.cFileName);
            }
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR *dir = opendir(base_config->batch_dir);
    if (!dir) {
        fprintf(stderr, "[-] Erro: Não foi possível abrir o diretório '%s'.\n", base_config->batch_dir);
        return 1;
    }
    size_t capacity = 64;
    size_t count = 0;
    char (*files)[512] = malloc(capacity * sizeof(*files));
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (is_supported_video_ext(entry->d_name)) {
            if (count >= capacity) {
                capacity *= 2;
                char (*new_files)[512] = realloc(files, capacity * sizeof(*files));
                if (!new_files) break;
                files = new_files;
            }
            snprintf(files[count++], 512, "%s", entry->d_name);
        }
    }
    closedir(dir);
#endif

    if (count == 0) {
        fprintf(stderr, "[-] Nenhum vídeo de CFTV compatível (.mp4, .mov, .dat, .dav, .264, etc.) foi localizado na pasta.\n");
        free(files);
        return 1;
    }

    fprintf(stdout, "[+] Localizados %zu arquivos de vídeo para restauração em massa.\n\n", count);

    BatchFileRecord *records = malloc(count * sizeof(BatchFileRecord));
    size_t success_count = 0;
    size_t needs_ref_count = 0;
    size_t fail_count = 0;
    int64_t total_bytes_batch = 0;
    int64_t total_frames_batch = 0;
    double batch_start = utils_get_time_sec();

    for (size_t i = 0; i < count; i++) {
        fprintf(stdout, "----------------------------------------------------------------------\n");
        fprintf(stdout, "[LOTE %zu/%zu] Processando arquivo: %s\n", i + 1, count, files[i]);
        fprintf(stdout, "----------------------------------------------------------------------\n");

        HealerConfig item_config = *base_config;
        snprintf(item_config.input_path, sizeof(item_config.input_path), "%.800s/%.200s", base_config->batch_dir, files[i]);

        char stem[512];
        snprintf(stem, sizeof(stem), "%s", files[i]);
        char *dot = strrchr(stem, '.');
        if (dot) *dot = '\0';

        snprintf(item_config.output_path, sizeof(item_config.output_path), "%.700s/%.200s_recuperado.mp4",
                 base_config->batch_out_dir, stem);

        HealerStats item_stats;
        int ret = healer_process(&item_config, &item_stats);

        BatchFileRecord *rec = &records[i];
        strncpy(rec->filename, files[i], sizeof(rec->filename) - 1);
        rec->file_size = item_stats.total_bytes;
        rec->frames = item_stats.recovered_frames;
        rec->width = item_stats.width;
        rec->height = item_stats.height;
        rec->method = item_stats.repair_mode_name ? item_stats.repair_mode_name : "--";
        rec->elapsed = item_stats.end_time - item_stats.start_time;

        if (ret == HEALER_SUCCESS && item_stats.recovered_frames > 0) {
            rec->success = 1;
            rec->needs_ref = 0;
            success_count++;
            total_frames_batch += item_stats.recovered_frames;
            total_bytes_batch += item_stats.total_bytes;
            fprintf(stdout, "\n[+] SUCESSO: %lld quadros recuperados (%dx%d) com FastStart!\n\n",
                    (long long)item_stats.recovered_frames, item_stats.width, item_stats.height);
        } else if (ret == HEALER_ERR_NEEDS_REFERENCE || item_stats.needs_reference) {
            rec->success = 0;
            rec->needs_ref = 1;
            needs_ref_count++;
            total_bytes_batch += item_stats.total_bytes;
            fprintf(stderr, "\n[!] PENDENTE: Requer vídeo de referência da mesma câmera para transplante.\n\n");
        } else {
            rec->success = 0;
            rec->needs_ref = 0;
            fail_count++;
            total_bytes_batch += item_stats.total_bytes;
            fprintf(stderr, "\n[-] FALHA: Não foram encontrados dados decodificáveis neste arquivo.\n\n");
        }
    }

    double batch_total_time = utils_get_time_sec() - batch_start;
    char tot_gb_str[32];
    utils_format_bytes(total_bytes_batch, tot_gb_str, sizeof(tot_gb_str));

    // Relatório Consolidado do Lote
    fprintf(stdout, "\n======================================================================\n");
    fprintf(stdout, "               RELATÓRIO CONSOLIDADO DO LOTE EM MASSA                 \n");
    fprintf(stdout, "                   FixCFTV by PladixOficial                           \n");
    fprintf(stdout, "======================================================================\n");
    fprintf(stdout, " Total de Arquivos Processados:   %zu\n", count);
    fprintf(stdout, " ✅ Restaurados com Sucesso:       %zu (%.1f%%)\n", success_count, ((double)success_count / (double)count) * 100.0);
    fprintf(stdout, " ⏳ Aguardando Vídeo de Referência: %zu\n", needs_ref_count);
    fprintf(stdout, " ❌ Sem Dados Decodificáveis:     %zu\n", fail_count);
    fprintf(stdout, " Volume Total de Dados:           %s\n", tot_gb_str);
    fprintf(stdout, " Total de Quadros Restaurados:    %lld quadros\n", (long long)total_frames_batch);
    fprintf(stdout, " Tempo Total de Execução:         %.2f segundos\n", batch_total_time);
    if (batch_total_time > 0.001) {
        double avg_mb_s = ((double)total_bytes_batch / (1024.0 * 1024.0)) / batch_total_time;
        fprintf(stdout, " Vazão Média do Sistema:          %.2f MB/s\n", avg_mb_s);
    }
    fprintf(stdout, " Diretório de Gravações Prontas:  %s\n", base_config->batch_out_dir);
    fprintf(stdout, "======================================================================\n\n");

    free(files);
    free(records);
    return (success_count > 0) ? 0 : 1;
}

int main(int argc, char *argv[]) {
    HealerConfig config;
    memset(&config, 0, sizeof(HealerConfig));
    config.default_fps = 25;

    utils_print_banner();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            strncpy(config.input_path, argv[++i], sizeof(config.input_path) - 1);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            strncpy(config.output_path, argv[++i], sizeof(config.output_path) - 1);
        } else if ((strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--batch") == 0) && i + 1 < argc) {
            strncpy(config.batch_dir, argv[++i], sizeof(config.batch_dir) - 1);
            config.batch_mode = 1;
        } else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            strncpy(config.batch_out_dir, argv[++i], sizeof(config.batch_out_dir) - 1);
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            strncpy(config.reference_path, argv[++i], sizeof(config.reference_path) - 1);
        } else if (strcmp(argv[i], "-f") == 0) {
            config.force_raw_scan = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            config.verbose = 1;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            config.default_fps = atoi(argv[++i]);
            if (config.default_fps <= 0) config.default_fps = 25;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "[-] Opção não reconhecida: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Modo em Massa (Batch)
    if (config.batch_mode) {
        if (!config.batch_dir[0]) {
            fprintf(stderr, "[-] Erro: O diretório de entrada para processamento em massa (-b) deve ser informado.\n\n");
            print_usage(argv[0]);
            return 1;
        }
        if (!config.batch_out_dir[0]) {
            if (config.output_path[0]) {
                snprintf(config.batch_out_dir, sizeof(config.batch_out_dir), "%.1000s", config.output_path);
            } else {
                snprintf(config.batch_out_dir, sizeof(config.batch_out_dir), "%.1000s/recuperados", config.batch_dir);
            }
        }
        return run_batch_mode(&config);
    }

    // Modo Individual
    if (!config.input_path[0]) {
        fprintf(stderr, "[-] Erro: O arquivo de entrada corrompido (-i) deve ser informado.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!config.output_path[0]) {
        fprintf(stderr, "[-] Erro: O arquivo de saída restaurado (-o) deve ser informado.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    fprintf(stdout, "[*] Arquivo Corrompido:     %s\n", config.input_path);
    fprintf(stdout, "[*] Arquivo Restaurado:     %s\n", config.output_path);
    if (config.reference_path[0]) {
        fprintf(stdout, "[*] Arquivo de Referência:  %s (Transplante de Parâmetros)\n", config.reference_path);
    } else {
        fprintf(stdout, "[*] Modo de Operação:       Cascata Autônoma Inteligente (Sem Referência Prévia)\n");
    }
    if (config.force_raw_scan) {
        fprintf(stdout, "[*] Modo de Recuperação:    Varredura Bruta de NAL Units forçada (-f)\n");
    }
    fprintf(stdout, "\n");

    HealerStats stats;
    int ret = healer_process(&config, &stats);

    if (ret == HEALER_ERR_NEEDS_REFERENCE || stats.needs_reference) {
        return 2;
    }

    if (ret != HEALER_SUCCESS || stats.recovered_frames == 0) {
        fprintf(stderr, "\n[-] AVISO: Não foram localizados quadros de vídeo decodificáveis no arquivo.\n");
        return 1;
    }

    utils_print_summary(&stats, &config);
    return 0;
}
