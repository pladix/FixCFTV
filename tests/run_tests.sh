#!/bin/sh
# ==============================================================================
# FixCFTV - Automated Test Suite
# Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
WORK_DIR="$SCRIPT_DIR/sandbox"
BIN="$ROOT_DIR/FixCFTV"

if [ -f "$ROOT_DIR/FixCFTV.exe" ]; then
    BIN="$ROOT_DIR/FixCFTV.exe"
fi

echo "======================================================================"
echo "          INICIANDO SUITE DE TESTES FORENSES (FixCFTV)                "
echo "                   Desenvolvido por PladixOficial                     "
echo "======================================================================"
echo "[*] Diretório do projeto: $ROOT_DIR"
echo "[*] Binário FixCFTV:      $BIN"

if [ ! -f "$BIN" ]; then
    echo "[-] Erro: Executável '$BIN' não encontrado. Compile com 'make' primeiro."
    exit 1
fi

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

# ------------------------------------------------------------------------------
# TESTE 1: MODO 2 - VARREDURA BRUTA DE NAL UNITS & FASTSTART REMUX
# Cenário: Queda abrupta de energia durante gravação em DVR (Intelbras/Dahua/Hikvision)
# O contêiner gravou mdat, mas foi interrompido antes de gravar o átomo moov.
# ------------------------------------------------------------------------------
echo ""
echo "----------------------------------------------------------------------"
echo "[TESTE 1] Simulação de Queda de Energia em DVR (Modo 2 - Varredura Bruta)"
echo "----------------------------------------------------------------------"

SYNTH_CCTV="$WORK_DIR/cctv_raw_camera.mp4"
CORRUPTED_CCTV="$WORK_DIR/cctv_power_failure.mp4"
RECOVERED_CCTV="$WORK_DIR/cctv_recovered_mode2.mp4"

echo "[1.1] Gerando stream H.264 simulando câmera CFTV (1920x1080 @ 25fps)..."
ffmpeg -y -loglevel warning -f lavfi -i testsrc=duration=4:size=1920x1080:rate=25 \
    -c:v libx264 -x264-params repeat-headers=1 -pix_fmt yuv420p "$SYNTH_CCTV"

TOTAL_SIZE=$(wc -c < "$SYNTH_CCTV" | tr -d ' ')
TRUNCATE_SIZE=$((TOTAL_SIZE - 2500))

echo "[1.2] Simulando corrupção por corte de energia (suprimindo átomo moov)..."
dd if="$SYNTH_CCTV" of="$CORRUPTED_CCTV" bs=1 count="$TRUNCATE_SIZE" 2>/dev/null || \
head -c "$TRUNCATE_SIZE" "$SYNTH_CCTV" > "$CORRUPTED_CCTV"

echo "[1.3] Validando que o arquivo corrompido é rejeitado por players comuns..."
if ffprobe -v error "$CORRUPTED_CCTV" 2>/dev/null; then
    echo "[-] Erro no teste: O arquivo corrompido não deveria ser lido!"
    exit 1
else
    echo "[+] Confirmado: Arquivo corrompido é ilegível ('moov atom not found')."
fi

echo "[1.4] Executando FixCFTV (Modo 2: Varredura de NAL Units)..."
"$BIN" -i "$CORRUPTED_CCTV" -o "$RECOVERED_CCTV" -f

echo "[1.5] Validando integridade do vídeo restaurado via ffprobe..."
ffprobe -v error -show_entries format=duration,probe_score -show_entries stream=codec_name,width,height,nb_frames -of default=noprint_wrappers=1 "$RECOVERED_CCTV"

echo "[1.6] Verificando decodificação de 100% dos quadros recuperados..."
ffmpeg -v error -i "$RECOVERED_CCTV" -f null -
echo "[+] TESTE 1 APROVADO COM SUCESSO! Vídeo recuperado e decodificado sem falhas."

# ------------------------------------------------------------------------------
# TESTE 2: MODO 1 - TRANSPLANTE DE METADADOS & SPS/PPS COM VÍDEO DE REFERÊNCIA
# ------------------------------------------------------------------------------
echo ""
echo "----------------------------------------------------------------------"
echo "[TESTE 2] Transplante de Metadados / SPS / PPS (Modo 1 - Vídeo de Referência)"
echo "----------------------------------------------------------------------"

REF_VIDEO="$WORK_DIR/camera_reference_good.mp4"
SYNTH_BROKEN="$WORK_DIR/camera_recording_full.mp4"
CORRUPTED_BROKEN="$WORK_DIR/camera_recording_broken.mp4"
RECOVERED_MODE1="$WORK_DIR/camera_recovered_mode1.mp4"

echo "[2.1] Gerando vídeo de referência funcional da câmera (1280x720 @ 30fps)..."
ffmpeg -y -loglevel warning -f lavfi -i testsrc=duration=3:size=1280x720:rate=30 \
    -c:v libx264 -pix_fmt yuv420p "$REF_VIDEO"

echo "[2.2] Gerando gravação com mesma câmera..."
ffmpeg -y -loglevel warning -f lavfi -i testsrc=duration=5:size=1280x720:rate=30 \
    -c:v libx264 -pix_fmt yuv420p "$SYNTH_BROKEN"

TOTAL_SIZE_2=$(wc -c < "$SYNTH_BROKEN" | tr -d ' ')
TRUNCATE_SIZE_2=$((TOTAL_SIZE_2 - 2500))

echo "[2.3] Quebrando o arquivo (removendo moov e parâmetros globais)..."
dd if="$SYNTH_BROKEN" of="$CORRUPTED_BROKEN" bs=1 count="$TRUNCATE_SIZE_2" 2>/dev/null || \
head -c "$TRUNCATE_SIZE_2" "$SYNTH_BROKEN" > "$CORRUPTED_BROKEN"

echo "[2.4] Executando FixCFTV com parâmetro de referência (-r)..."
"$BIN" -i "$CORRUPTED_BROKEN" -o "$RECOVERED_MODE1" -r "$REF_VIDEO"

echo "[2.5] Validando vídeo restaurado via ffprobe..."
ffprobe -v error -show_entries format=duration,probe_score -show_entries stream=codec_name,width,height,nb_frames -of default=noprint_wrappers=1 "$RECOVERED_MODE1"

echo "[2.6] Verificando integridade de decodificação completa..."
ffmpeg -v error -i "$RECOVERED_MODE1" -f null -
echo "[+] TESTE 2 APROVADO COM SUCESSO! Transplante de metadados funcional e íntegro."

echo ""
echo "======================================================================"
echo "          TODOS OS TESTES FORAM CONCLUÍDOS COM 100% DE SUCESSO!       "
echo "======================================================================"
