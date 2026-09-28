#ifndef FIXCFTV_HEALER_H
#define FIXCFTV_HEALER_H

/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#include "utils.h"

#define HEALER_SUCCESS              0
#define HEALER_ERR_FAILED          -1
#define HEALER_ERR_NEEDS_REFERENCE  2

/**
 * Ponto de entrada principal para o processo de recuperação forense.
 * Executa automaticamente o diagnóstico e a cascata de recuperação com múltiplos métodos internos.
 * Somente solicita um vídeo de referência se todos os métodos autônomos internos falharem.
 */
int healer_process(const HealerConfig *config, HealerStats *stats);

/**
 * Método 1: Restauração Permissiva de Contêiner & Re-indexação FastStart.
 * Tenta demuxing tolerante com libavformat com sincronismo forçado.
 */
int healer_repair_permissive_container(const HealerConfig *config, HealerStats *stats);

/**
 * Método 2: Varredura Bruta de NAL Units & Reconstrução Annex B com FastStart.
 * Percorre o fluxo mdat byte a byte, recupera unidades NAL válidas com SPS/PPS embutidos.
 */
int healer_repair_raw_scan(const HealerConfig *config, HealerStats *stats);

/**
 * Método 3: Desempacotamento de Payload AVCC (comprimento de 4 bytes) para Annex B.
 */
int healer_repair_avcc(const HealerConfig *config, HealerStats *stats);

/**
 * Método 4: Desencapsulador Dahua / Intelbras DHAV (.dav ou pacotes DVR com tags DHAV).
 */
int healer_repair_dhav(const HealerConfig *config, HealerStats *stats);

/**
 * Método 5: Injeção de Parâmetros SPS/PPS Sintéticos Heurísticos (quando ausentes no fluxo).
 */
int healer_repair_synthetic_params(const HealerConfig *config, HealerStats *stats);

/**
 * Método de Fallback / Modo Referência: Transplante de Metadados / SPS / PPS via vídeo da mesma câmera.
 */
int healer_repair_with_reference(const HealerConfig *config, HealerStats *stats);

#endif // FIXCFTV_HEALER_H
