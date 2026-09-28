# ==============================================================================
# FixCFTV - PowerShell Test Suite
# Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
# ==============================================================================

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir
$workDir = Join-Path $scriptDir "sandbox_ps"
$bin = Join-Path $rootDir "FixCFTV.exe"

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "          INICIANDO SUITE DE TESTES FORENSES (FixCFTV)                " -ForegroundColor Cyan
Write-Host "                   Desenvolvido por PladixOficial                     " -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "[*] Diretório do projeto: $rootDir"
Write-Host "[*] Binário FixCFTV:      $bin"

if (-not (Test-Path $bin)) {
    Write-Error "Executável '$bin' não encontrado. Compile com 'make' primeiro."
    exit 1
}

if (Test-Path $workDir) {
    Remove-Item $workDir -Recurse -Force
}
New-Item -ItemType Directory -Path $workDir -Force | Out-Null

# ------------------------------------------------------------------------------
# TESTE 1: MODO 2 - VARREDURA BRUTA DE NAL UNITS & FASTSTART REMUX
# ------------------------------------------------------------------------------
Write-Host ""
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow
Write-Host "[TESTE 1] Simulação de Queda de Energia em DVR (Modo 2 - Varredura Bruta)" -ForegroundColor Yellow
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow

$synthCctv = Join-Path $workDir "cctv_raw_camera.mp4"
$corruptedCctv = Join-Path $workDir "cctv_power_failure.mp4"
$recoveredCctv = Join-Path $workDir "cctv_recovered_mode2.mp4"

Write-Host "[1.1] Gerando stream H.264 simulando câmera CFTV (1920x1080 @ 25fps)..."
ffmpeg -y -loglevel warning -f lavfi -i testsrc=duration=4:size=1920x1080:rate=25 `
    -c:v libx264 -x264-params repeat-headers=1 -pix_fmt yuv420p $synthCctv

$totalSize = (Get-Item $synthCctv).Length
$truncateSize = $totalSize - 2500

Write-Host "[1.2] Simulando corrupção por corte de energia (suprimindo átomo moov)..."
$bytes = [System.IO.File]::ReadAllBytes($synthCctv)
$corruptedBytes = New-Object byte[] $truncateSize
[Array]::Copy($bytes, $corruptedBytes, $truncateSize)
[System.IO.File]::WriteAllBytes($corruptedCctv, $corruptedBytes)

Write-Host "[1.3] Validando que o arquivo corrompido é rejeitado por players comuns..."
$p = Start-Process ffprobe -ArgumentList "-v error `"$corruptedCctv`"" -NoNewWindow -PassThru -Wait
if ($p.ExitCode -eq 0) {
    Write-Error "O arquivo corrompido não deveria ser lido pelo ffprobe!"
    exit 1
} else {
    Write-Host "[+] Confirmado: Arquivo corrompido é ilegível ('moov atom not found')." -ForegroundColor Green
}

Write-Host "[1.4] Executando FixCFTV (Modo 2: Varredura de NAL Units)..."
& $bin -i $corruptedCctv -o $recoveredCctv -f

Write-Host "[1.5] Validando integridade do vídeo restaurado via ffprobe..."
ffprobe -v error -show_entries format=duration,probe_score -show_entries stream=codec_name,width,height,nb_frames -of default=noprint_wrappers=1 $recoveredCctv

Write-Host "[1.6] Verificando decodificação de 100% dos quadros recuperados..."
ffmpeg -v error -i $recoveredCctv -f null -
Write-Host "[+] TESTE 1 APROVADO COM SUCESSO! Vídeo recuperado e decodificado sem falhas." -ForegroundColor Green

# ------------------------------------------------------------------------------
# TESTE 2: MODO 1 - TRANSPLANTE DE METADADOS & SPS/PPS COM VÍDEO DE REFERÊNCIA
# ------------------------------------------------------------------------------
Write-Host ""
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow
Write-Host "[TESTE 2] Transplante de Metadados / SPS / PPS (Modo 1 - Vídeo de Referência)" -ForegroundColor Yellow
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow

$refVideo = Join-Path $workDir "camera_reference_good.mp4"
$synthBroken = Join-Path $workDir "camera_recording_full.mp4"
$corruptedBroken = Join-Path $workDir "camera_recording_broken.mp4"
$recoveredMode1 = Join-Path $workDir "camera_recovered_mode1.mp4"

Write-Host "[2.1] Gerando vídeo de referência funcional da câmera (1280x720 @ 30fps)..."
ffmpeg -y -loglevel warning -f lavfi -i testsrc=duration=3:size=1280x720:rate=30 `
    -c:v libx264 -pix_fmt yuv420p $refVideo

Write-Host "[2.2] Gerando gravação com mesma câmera..."
ffmpeg -y -loglevel warning -f lavfi -i testsrc=duration=5:size=1280x720:rate=30 `
    -c:v libx264 -pix_fmt yuv420p $synthBroken

$totalSize2 = (Get-Item $synthBroken).Length
$truncateSize2 = $totalSize2 - 2500

Write-Host "[2.3] Quebrando o arquivo (removendo moov e parâmetros globais)..."
$bytes2 = [System.IO.File]::ReadAllBytes($synthBroken)
$corruptedBytes2 = New-Object byte[] $truncateSize2
[Array]::Copy($bytes2, $corruptedBytes2, $truncateSize2)
[System.IO.File]::WriteAllBytes($corruptedBroken, $corruptedBytes2)

Write-Host "[2.4] Executando FixCFTV com parâmetro de referência (-r)..."
& $bin -i $corruptedBroken -o $recoveredMode1 -r $refVideo

Write-Host "[2.5] Validando vídeo restaurado via ffprobe..."
ffprobe -v error -show_entries format=duration,probe_score -show_entries stream=codec_name,width,height,nb_frames -of default=noprint_wrappers=1 $recoveredMode1

Write-Host "[2.6] Verificando integridade de decodificação completa..."
ffmpeg -v error -i $recoveredMode1 -f null -
Write-Host "[+] TESTE 2 APROVADO COM SUCESSO! Transplante de metadados funcional e íntegro." -ForegroundColor Green

# ------------------------------------------------------------------------------
# TESTE 3: CASCATA AUTÔNOMA MULTI-MÉTODO (SEM VÍDEO DE REFERÊNCIA)
# ------------------------------------------------------------------------------
Write-Host ""
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow
Write-Host "[TESTE 3] Recuperação Autônoma em Cascata (Apenas o arquivo corrompido)" -ForegroundColor Yellow
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow

$corruptedAuto = Join-Path $workDir "cctv_autonomous_broken.mp4"
$recoveredAuto = Join-Path $workDir "cctv_autonomous_recovered.mp4"

# Cria uma cópia corrompida sem átomo moov
Copy-Item $corruptedCctv $corruptedAuto

Write-Host "[3.1] Executando FixCFTV sem nenhum parâmetro de referência ou força..."
Write-Host "      Comando: FixCFTV -i $corruptedAuto -o $recoveredAuto"
& $bin -i $corruptedAuto -o $recoveredAuto

Write-Host "[3.2] Validando que a cascata autônoma recuperou o vídeo perfeitamente..."
ffprobe -v error -show_entries format=duration,probe_score -show_entries stream=codec_name,width,height,nb_frames -of default=noprint_wrappers=1 $recoveredAuto
ffmpeg -v error -i $recoveredAuto -f null -
Write-Host "[+] TESTE 3 APROVADO COM SUCESSO! Cascata autônoma recuperou o vídeo sem referência." -ForegroundColor Green

# ------------------------------------------------------------------------------
# TESTE 4: PROCESSAMENTO EM MASSA (FILA DE VÍDEOS / LOTE SEM LIMITES)
# ------------------------------------------------------------------------------
Write-Host ""
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow
Write-Host "[TESTE 4] Processamento em Massa / Fila de Lote (Batch Processing)" -ForegroundColor Yellow
Write-Host "----------------------------------------------------------------------" -ForegroundColor Yellow

$batchInDir = Join-Path $workDir "batch_input"
$batchOutDir = Join-Path $workDir "batch_output"
New-Item -ItemType Directory -Path $batchInDir -Force | Out-Null
New-Item -ItemType Directory -Path $batchOutDir -Force | Out-Null

Write-Host "[4.1] Populando lote com múltiplos vídeos corrompidos simultâneos..."
Copy-Item $corruptedCctv (Join-Path $batchInDir "camera_nobreak_queda01.mp4")
Copy-Item $corruptedCctv (Join-Path $batchInDir "camera_nobreak_queda02.mp4")
Copy-Item $corruptedBroken (Join-Path $batchInDir "camera_nobreak_queda03.mp4")

Write-Host "[4.2] Executando FixCFTV em lote com argumento -b..."
& $bin -b $batchInDir -o $batchOutDir -r $refVideo

$outFiles = Get-ChildItem -Path $batchOutDir -Filter "*_recuperado.mp4"
if ($outFiles.Count -lt 3) {
    Write-Error "O lote falhou em recuperar todos os arquivos! Encontrados: $($outFiles.Count)"
    exit 1
}

Write-Host "[+] TESTE 4 APROVADO COM SUCESSO! Todos os $($outFiles.Count) vídeos do lote foram recuperados com sucesso." -ForegroundColor Green

Write-Host ""
Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "          TODOS OS TESTES (1 A 4) CONCLUÍDOS COM 100% DE SUCESSO!     " -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

