# FixCFTV 🛡️

<div align="center">

[![Linguagem](https://img.shields.io/badge/Linguagem-C99%20%2F%20C11-0284c7.svg?style=for-the-badge&logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![FFmpeg](https://img.shields.io/badge/FFmpeg-Libav%20Core-007800.svg?style=for-the-badge&logo=ffmpeg)](https://ffmpeg.org)
[![Plataformas](https://img.shields.io/badge/Plataformas-Windows%20%7C%20Linux-4f46e5.svg?style=for-the-badge)](https://github.com/pladix/FixCFTV)
[![Licença](https://img.shields.io/badge/Licen%C3%A7a-MIT-10b981.svg?style=for-the-badge)](LICENSE)
[![Autor](https://img.shields.io/badge/Autor-PladixOficial-f59e0b.svg?style=for-the-badge&logo=github)](https://github.com/pladix)

**Ferramenta Forense de Baixo Nível em C para Recuperação, Diagnóstico e Reconstrução de Vídeos Corrompidos de Segurança (CFTV, DVR, NVR e Câmeras IP).**

[O Problema](#-o-problema-resolvido) •
[Cascata Autônoma](#-cascata-autônoma-inteligente-multi-método) •
[Fila em Massa](#-processamento-em-massa-sem-limites) •
[Interface Gráfica](#-interface-gráfica-moderna) •
[Compilação & Testes](#-compilação-e-testes) •
[Como Usar](#-como-usar) •
[Fabricantes](#-guia-por-fabricante)

</div>

---

## 📹 O Problema Resolvido

Em sistemas de segurança eletrônica (CFTV), quedas de energia, desligamentos abruptos de nobreaks, desconexões de rede e travamentos de DVR geram um problema clássico:

1. O gravador escreve o fluxo bruto de vídeo continuamente no bloco **`mdat`** (*media data*).
2. No momento do desligamento repentino, o gravador é interrompido antes de gravar ou fechar o átomo de metadados **`moov`**.
3. O átomo `moov` armazena as tabelas de alocação de quadros (`stco`/`co64`, `stsz`, `stts`, `stsc`) e os parâmetros vitais do sensor da câmera (**SPS - Sequence Parameter Set** e **PPS - Picture Parameter Set**).
4. O arquivo gerado no disco possui o tamanho real da gravação (às vezes centenas de megabytes ou dezenas de gigabytes), mas **não abre em nenhum player** (VLC, Windows Media Player, QuickTime, navegadores), retornando erros como:
   ```text
   [mov,mp4,m4a,3gp,3g2,mj2 @ ...] moov atom not found
   corrupted.mp4: Invalid data found when processing input
   ```

O **FixCFTV** foi projetado para atuar de forma cirúrgica: o usuário simplesmente fornece o arquivo quebrado. O sistema diagnostica a causa da falha e executa uma **cascata autônoma de recuperação** com múltiplos métodos internos. Apenas se todos os métodos autônomos internos falharem é que o programa solicita um vídeo de referência da mesma câmera para transplante.

---

## ⚡ Cascata Autônoma Inteligente (Multi-Método)

Ao receber o arquivo danificado, o **FixCFTV** analisa a estrutura interna e tenta recuperar os quadros automaticamente através de 5 estratégias sequenciais, **sem exigir um vídeo original inicialmente**:

1. **Método 1 - Restauração Permissiva de Contêiner & Re-indexação:**
   - Tenta abrir o contêiner com tolerância a corrupção máxima (`genpts`, `discardcorrupt`, `nobuffer`, `ignore_err`), reparando fragmentos e reconstruindo os índices para MP4 FastStart.
2. **Método 2 - Varredura Bruta de NAL Units (Annex B):**
   - Varre o payload `mdat` byte a byte procurando códigos de início `00 00 01` e `00 00 00 01`. Extrai os parâmetros SPS/PPS emitidos internamente pela câmera a cada quadro-chave (IDR) e reconstrói o vídeo completo.
3. **Método 3 - Desempacotamento de Blocos AVCC (Comprimento de 4 bytes):**
   - Converte dinamicamente blocos de comprimento AVCC para start codes Annex B em fluxo contínuo de 1 MB, tratando quebras de limite de setores e descartando fatias truncadas no fim do arquivo.
4. **Método 4 - Desencapsulador Dahua / Intelbras DHAV:**
   - Detecta e decodifica envelopes proprietários DHAV presentes em arquivos `.dav` ou gravações brutas da Dahua e Intelbras, removendo cabeçalhos de controle e extraindo o bitstream elementar puro.
5. **Método 5 - Injeção de Parâmetros SPS/PPS Sintéticos Heurísticos:**
   - Caso a câmera nunca tenha emitido SPS/PPS no meio do fluxo e o cabeçalho moov tenha sido perdido, o FixCFTV sintetiza parâmetros compatíveis de sensor (1080p, 720p, 4MP) para permitir a decodificação imediata das fatias órfãs.
6. **Fallback Inteligente (Vídeo de Referência):**
   - Somente se todos os 5 métodos internos acima falharem, o sistema emite um alerta humanizado solicitando um vídeo de poucos segundos gravado pelo mesmo DVR/canal para clonar os metadados do sensor (Modo Transplante).

---

## 🚀 Processamento em Massa (Sem Limites)

- **Sem Limite de Tamanho:** Processamento em blocos contínuos de streaming (1 MB por iteração com offsets de 64-bit). O consumo de memória RAM permanece estritamente inferior a **25 MB**, seja processando um clipe de 10 MB ou imagens de disco de **500 GB a 2 TB**.
- **Fila em Massa Ilimitada:**
  - Pelo terminal: processa diretórios inteiros via `-b <pasta_com_videos> -o <pasta_destino>`.
  - Pela interface gráfica: aba dedicada com tabela visual, adição de múltiplos arquivos simultâneos, pastas completas, progresso individual e global, e relatório consolidado com estatísticas de vazão.

---

## 🖥️ Interface Gráfica Moderna & Responsiva

Desenvolvida com foco em técnicos de CFTV, instaladores e peritos forenses:

- **100% Compatível com Qualquer Escala High-DPI no Windows:** Texto nítido em 1080p, 1440p (2K) e 4K.
- **Tema Dark Moderno:** Paleta de cores Slate / Indigo / Cyan / Emerald com alto contraste e legibilidade.
- **5 Abas Funcionais:**
  - 📁 **Aba 1 (Recuperação Individual):** Seleção rápida, diagnóstico automático de falha em tempo real, cascata autônoma e barra de progresso com vazão em MB/s e estimativa de término.
  - 📚 **Aba 2 (Fila em Massa / Lote):** Tabela completa com colunas de status, método que recuperou cada vídeo, resolução, quadros, tempo e ações contextuais.
  - 📊 **Aba 3 (Relatório & Diagnóstico Forense):** Cards com resumo técnico, console de logs detalhados e botão para exportar laudo em TXT.
  - 🎬 **Aba 4 (Player & Validação):** Reprodução instantânea no player padrão do Windows e teste de integridade quadro a quadro com FFprobe.
  - 📹 **Aba 5 (Guia Técnico de Câmeras):** Recomendações práticas para Intelbras, Dahua, Hikvision, AITEK e XM.

---

## 🛠️ Compilação e Testes

### Compilar Tudo (CLI + GUI Win32):
```bash
make all
```

### Compilar apenas o CLI:
```bash
make cli
```

### Executar a Suíte Completa de Testes Automatizados:
```bash
make test
# Ou diretamente no PowerShell:
pwsh -ExecutionPolicy Bypass -File tests/run_tests.ps1
```

A suíte de testes valida automaticamente:
1. Simulação de queda de energia em gravação 1080p (Varredura bruta e remux FastStart).
2. Transplante de metadados e SPS/PPS com vídeo de referência.
3. Recuperação autônoma em cascata sem parâmetros ou flags prévias.
4. Processamento de lote em massa com múltiplos arquivos simultâneos.

---

## 📖 Como Usar

### 1. Pela Interface Gráfica

Basta dar um duplo clique em:
👉 **`FixCFTV.bat`** (Abre a interface moderna completa)  
ou  
👉 **`FixCFTV_Native.bat`** (Abre o executável Win32 nativo ultraleve)

1. Selecione o vídeo quebrado na aba **Recuperação Individual** ou adicione múltiplos arquivos na aba **Fila em Massa**.
2. Clique em **`⚡ INICIAR RESTAURAÇÃO AUTÔNOMA`**.
3. O FixCFTV detecta o formato, executa a cascata interna e salva o novo MP4 com FastStart pronto para assistir!

---

### 2. Pela Linha de Comando (CLI)

```bash
# Recuperação autônoma de um único arquivo (testa os métodos internos automaticamente)
./FixCFTV -i gravacao_queda_energia.mp4 -o gravacao_recuperada.mp4

# Processamento de pasta inteira em massa (Batch Mode)
./FixCFTV -b D:\Gravacoes_Quebradas -o D:\Videos_Recuperados

# Transplante direto com vídeo de referência da mesma câmera
./FixCFTV -i corrompido.mp4 -o recuperado.mp4 -r video_bom_mesma_camera.mp4

# Varredura bruta forçada de dump de disco (.dat ou .raw) com logs verbosos
./FixCFTV -i particao_dvr.dat -o camera_recuperada.mp4 -f -v
```

### Tabela de Parâmetros:

| Flag | Descrição |
| :--- | :--- |
| `-i <arquivo>` | Caminho do arquivo quebrado ou corrompido individual. |
| `-o <arquivo/pasta>` | Caminho do novo vídeo MP4 gerado ou diretório de saída no modo lote. |
| `-b, --batch <pasta>`| Processa automaticamente todos os vídeos de uma pasta em lote sem limites. |
| `-r <arquivo>` | Vídeo de referência funcional gravado pelo mesmo DVR/canal (Modo Transplante). |
| `-f` | Força a varredura bruta de unidades NAL. |
| `--fps <n>` | Taxa de quadros padrão caso não identificada no stream (Padrão: 25). |
| `-v, --verbose` | Ativa saída de logs detalhados e diagnósticos forenses no terminal. |
| `-h, --help` | Exibe a ajuda com parâmetros e exemplos de uso. |

---

## 📹 Guia por Fabricante

| Fabricante | Linhas Típicas | Causa Comum da Falha | Comportamento do FixCFTV |
| :--- | :--- | :--- | :--- |
| **Intelbras** | Linhas MHDX, Multi HD, iVD, NVD, Veiculares | Queda de energia corta gravação no meio do bloco `mdat`. | Detecta o corte e recupera autonomamente via Varredura NAL / AVCC. Se a câmera não emitiu parâmetros, solicita um clipe bom de poucos segundos para transplante. |
| **Dahua** | HDCVI, NVRs IP Séries 4xxx/5xxx, Arquivos `.dav` | Envelope proprietário DHAV e áudio G.711a intercalado. | O método DHAV isola os blocos de controle proprietários e reconstrói o fluxo MP4 universal. |
| **Hikvision** | TurboHD, AcuSense, ColorVu, NVRs IP | Interrupção repentina de gravação contínua. | A Hikvision repete SPS/PPS a cada quadro-chave IDR. O FixCFTV recupera 100% de forma autônoma. |
| **AITEK / XM** | XMEye, DVRs H.264 Genéricos, Câmeras Wi-Fi | Contêiner fechado sem tamanho no cabeçalho do `mdat`. | O FixCFTV localiza o início real dos quadros e gera os átomos `moov` com FastStart. |

---

## 📄 Licença

Este projeto é disponibilizado sob a licença **MIT**. Consulte o arquivo [LICENSE](LICENSE) para mais detalhes.

---

<div align="center">

Desenvolvido por **[PladixOficial](https://github.com/pladix)**  
Repositório Oficial: **[github.com/pladix/FixCFTV](https://github.com/pladix/FixCFTV)**

</div>
