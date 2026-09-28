/**
 * FixCFTV - CCTV & Security Video Restoration Engine
 * Interface Gráfica Nativa Windows (Win32 API)
 * Desenvolvido por PladixOficial (https://github.com/pladix/FixCFTV)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "healer.h"
#include "utils.h"

#define IDC_TAB_MAIN        1001
#define IDC_EDT_INPUT       1002
#define IDC_BTN_BROWSE_IN   1003
#define IDC_EDT_OUTPUT      1004
#define IDC_BTN_BROWSE_OUT  1005
#define IDC_EDT_REF         1006
#define IDC_BTN_BROWSE_REF  1007
#define IDC_CHK_FORCE_RAW   1008
#define IDC_BTN_START       1009
#define IDC_PROGRESS_BAR    1010
#define IDC_TXT_STATUS      1011
#define IDC_EDT_LOG         1012

static HINSTANCE g_hInst = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hTab = NULL;
static HWND g_hEdtIn = NULL, g_hEdtOut = NULL, g_hEdtRef = NULL;
static HWND g_hChkForce = NULL, g_hBtnStart = NULL, g_hProgress = NULL;
static HWND g_hTxtStatus = NULL, g_hEdtLog = NULL;
static HWND g_hLblIn = NULL, g_hLblOut = NULL, g_hLblRef = NULL;
static HWND g_hBtnIn = NULL, g_hBtnOut = NULL, g_hBtnRef = NULL;

static int g_current_tab = 0;
static volatile BOOL g_is_running = FALSE;

typedef struct {
    HealerConfig config;
    HealerStats stats;
} RepairThreadParams;

static void AppendLog(const char *text) {
    if (!g_hEdtLog || !text) return;
    int len = GetWindowTextLength(g_hEdtLog);
    SendMessage(g_hEdtLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(g_hEdtLog, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(g_hEdtLog, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
}

static DWORD WINAPI RepairWorkerThread(LPVOID lpParam) {
    RepairThreadParams *p = (RepairThreadParams *)lpParam;

    SendMessage(g_hProgress, PBM_SETRANGE32, 0, 100);
    SendMessage(g_hProgress, PBM_SETPOS, 25, 0);
    SetWindowTextA(g_hTxtStatus, "Analisando fluxo mdat e unidades NAL...");

    AppendLog("=================================================");
    AppendLog("[*] FixCFTV by PladixOficial - Restauração Iniciada");
    AppendLog(p->config.input_path);
    AppendLog("=================================================");

    int ret = healer_process(&p->config, &p->stats);

    SendMessage(g_hProgress, PBM_SETPOS, 100, 0);

    if (ret == 0 && p->stats.recovered_frames > 0) {
        char msg[1536];
        snprintf(msg, sizeof(msg),
                 "Restauração Concluída com Sucesso!\r\n\r\n"
                 "Arquivo: %s\r\n"
                 "Quadros Recuperados: %lld\r\n"
                 "Resolução: %dx%d pixels\r\n"
                 "Codec: %s\r\n"
                 "Contêiner: FastStart (Pronto para reprodução)",
                 p->config.output_path,
                 (long long)p->stats.recovered_frames,
                 p->stats.width, p->stats.height,
                 utils_codec_name(p->stats.codec_id));
        SetWindowTextA(g_hTxtStatus, "✅ Vídeo restaurado com sucesso! Arquivo funcional.");
        AppendLog(msg);
        MessageBoxA(g_hMainWnd, msg, "FixCFTV - Restauração Concluída", MB_ICONINFORMATION | MB_OK);
    } else if (ret == HEALER_ERR_NEEDS_REFERENCE || p->stats.needs_reference) {
        SetWindowTextA(g_hTxtStatus, "⏳ Métodos autônomos esgotados. Requer vídeo de referência.");
        AppendLog("[!] AVISO: Todos os métodos autônomos internos falharam por ausência de parâmetros do sensor.");
        AppendLog("[*] Forneça um vídeo funcional gravado pela mesma câmera para clonar os parâmetros SPS/PPS.");
        MessageBoxA(g_hMainWnd,
                    "Todos os métodos internos de recuperação autônoma foram tentados sem sucesso.\r\n\r\n"
                    "O arquivo perdeu completamente a descrição de parâmetros do sensor óptico da câmera.\r\n\r\n"
                    "Por favor, selecione um vídeo de referência funcional gravado pelo mesmo DVR/canal no campo abaixo.",
                    "FixCFTV - Vídeo de Referência Necessário", MB_ICONWARNING | MB_OK);
    } else {
        SetWindowTextA(g_hTxtStatus, "❌ Falha na recuperação de quadros.");
        AppendLog("[-] Erro: Não foram encontrados quadros válidos no fluxo.");
        MessageBoxA(g_hMainWnd, "Não foi possível extrair quadros válidos deste arquivo.\r\nExperimente selecionar um vídeo íntegro da mesma câmera como referência.",
                    "FixCFTV - Aviso", MB_ICONERROR | MB_OK);
    }

    g_is_running = FALSE;
    EnableWindow(g_hBtnStart, TRUE);
    free(p);
    return 0;
}

static void OnBrowseInput(HWND hwnd) {
    char szFile[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Vídeos CFTV / DVR (*.mp4;*.mov;*.dat;*.264)\0*.mp4;*.mov;*.dat;*.264;*.h264;*.raw\0Todos os Arquivos (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        SetWindowTextA(g_hEdtIn, szFile);
        char szOut[MAX_PATH] = {0};
        char *dot = strrchr(szFile, '.');
        if (dot) {
            size_t base_len = dot - szFile;
            snprintf(szOut, sizeof(szOut), "%.*s_restaurado.mp4", (int)base_len, szFile);
        } else {
            snprintf(szOut, sizeof(szOut), "%s_restaurado.mp4", szFile);
        }
        SetWindowTextA(g_hEdtOut, szOut);
    }
}

static void OnBrowseOutput(HWND hwnd) {
    char szFile[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Vídeo MP4 (*.mp4)\0*.mp4\0Todos os Arquivos (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameA(&ofn)) {
        SetWindowTextA(g_hEdtOut, szFile);
    }
}

static void OnBrowseRef(HWND hwnd) {
    char szFile[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Vídeo MP4/MOV (*.mp4;*.mov)\0*.mp4;*.mov\0Todos os Arquivos (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        SetWindowTextA(g_hEdtRef, szFile);
    }
}

static void OnStartRepair(HWND hwnd) {
    if (g_is_running) return;

    RepairThreadParams *p = (RepairThreadParams *)calloc(1, sizeof(RepairThreadParams));
    if (!p) return;

    GetWindowTextA(g_hEdtIn, p->config.input_path, sizeof(p->config.input_path));
    GetWindowTextA(g_hEdtOut, p->config.output_path, sizeof(p->config.output_path));
    GetWindowTextA(g_hEdtRef, p->config.reference_path, sizeof(p->config.reference_path));

    if (strlen(p->config.input_path) == 0) {
        MessageBoxA(hwnd, "Por favor, selecione o arquivo corrompido que deseja restaurar.", "Aviso", MB_ICONWARNING | MB_OK);
        free(p);
        return;
    }

    if (strlen(p->config.output_path) == 0) {
        MessageBoxA(hwnd, "Por favor, escolha onde salvar o novo arquivo recuperado.", "Aviso", MB_ICONWARNING | MB_OK);
        free(p);
        return;
    }

    p->config.force_raw_scan = (SendMessage(g_hChkForce, BM_GETCHECK, 0, 0) == BST_CHECKED);
    p->config.default_fps = 25;
    p->config.verbose = 1;

    g_is_running = TRUE;
    EnableWindow(g_hBtnStart, FALSE);
    SetWindowTextA(g_hTxtStatus, "Iniciando motor de reconstrução em C...");

    CreateThread(NULL, 0, RepairWorkerThread, p, 0, NULL);
}

static void UpdateTabVisibility(void) {
    int show_repair = (g_current_tab == 0) ? SW_SHOW : SW_HIDE;
    int show_diag   = (g_current_tab == 1) ? SW_SHOW : SW_HIDE;

    ShowWindow(g_hLblIn, show_repair);
    ShowWindow(g_hEdtIn, show_repair);
    ShowWindow(g_hBtnIn, show_repair);
    ShowWindow(g_hLblOut, show_repair);
    ShowWindow(g_hEdtOut, show_repair);
    ShowWindow(g_hBtnOut, show_repair);
    ShowWindow(g_hLblRef, show_repair);
    ShowWindow(g_hEdtRef, show_repair);
    ShowWindow(g_hBtnRef, show_repair);
    ShowWindow(g_hChkForce, show_repair);
    ShowWindow(g_hBtnStart, show_repair);
    ShowWindow(g_hProgress, show_repair);
    ShowWindow(g_hTxtStatus, show_repair);

    ShowWindow(g_hEdtLog, show_diag);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            InitCommonControls();

            HFONT hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            HFONT hBoldFont = CreateFontA(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            g_hTab = CreateWindowExA(0, WC_TABCONTROLA, "",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                     10, 10, 660, 480, hwnd, (HMENU)IDC_TAB_MAIN, g_hInst, NULL);
            SendMessage(g_hTab, WM_SETFONT, (WPARAM)hFont, TRUE);

            TCITEMA tie;
            tie.mask = TCIF_TEXT;
            tie.pszText = "🛠️ Recuperação de Vídeo";
            TabCtrl_InsertItem(g_hTab, 0, &tie);
            tie.pszText = "📊 Diagnóstico & Logs";
            TabCtrl_InsertItem(g_hTab, 1, &tie);
            tie.pszText = "ℹ️ Sobre o FixCFTV";
            TabCtrl_InsertItem(g_hTab, 2, &tie);

            int y = 55;
            g_hLblIn = CreateWindowExA(0, "STATIC", "Arquivo Corrompido (DVR / NVR / MP4 / DAT):",
                                       WS_CHILD | WS_VISIBLE, 25, y, 480, 20, hwnd, NULL, g_hInst, NULL);
            SendMessage(g_hLblIn, WM_SETFONT, (WPARAM)hFont, TRUE);

            y += 22;
            g_hEdtIn = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 25, y, 480, 25, hwnd, (HMENU)IDC_EDT_INPUT, g_hInst, NULL);
            SendMessage(g_hEdtIn, WM_SETFONT, (WPARAM)hFont, TRUE);

            g_hBtnIn = CreateWindowExA(0, "BUTTON", "📂 Procurar...",
                                       WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 515, y, 130, 26, hwnd, (HMENU)IDC_BTN_BROWSE_IN, g_hInst, NULL);
            SendMessage(g_hBtnIn, WM_SETFONT, (WPARAM)hFont, TRUE);

            y += 40;
            g_hLblOut = CreateWindowExA(0, "STATIC", "Salvar Vídeo Restaurado como (MP4 com FastStart):",
                                        WS_CHILD | WS_VISIBLE, 25, y, 480, 20, hwnd, NULL, g_hInst, NULL);
            SendMessage(g_hLblOut, WM_SETFONT, (WPARAM)hFont, TRUE);

            y += 22;
            g_hEdtOut = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 25, y, 480, 25, hwnd, (HMENU)IDC_EDT_OUTPUT, g_hInst, NULL);
            SendMessage(g_hEdtOut, WM_SETFONT, (WPARAM)hFont, TRUE);

            g_hBtnOut = CreateWindowExA(0, "BUTTON", "💾 Salvar em...",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 515, y, 130, 26, hwnd, (HMENU)IDC_BTN_BROWSE_OUT, g_hInst, NULL);
            SendMessage(g_hBtnOut, WM_SETFONT, (WPARAM)hFont, TRUE);

            y += 40;
            g_hLblRef = CreateWindowExA(0, "STATIC", "Vídeo de Referência (Modo 1 - gravado pelo mesmo DVR/canal):",
                                        WS_CHILD | WS_VISIBLE, 25, y, 550, 20, hwnd, NULL, g_hInst, NULL);
            SendMessage(g_hLblRef, WM_SETFONT, (WPARAM)hFont, TRUE);

            y += 22;
            g_hEdtRef = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 25, y, 480, 25, hwnd, (HMENU)IDC_EDT_REF, g_hInst, NULL);
            SendMessage(g_hEdtRef, WM_SETFONT, (WPARAM)hFont, TRUE);

            g_hBtnRef = CreateWindowExA(0, "BUTTON", "📂 Referência...",
                                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 515, y, 130, 26, hwnd, (HMENU)IDC_BTN_BROWSE_REF, g_hInst, NULL);
            SendMessage(g_hBtnRef, WM_SETFONT, (WPARAM)hFont, TRUE);

            y += 38;
            g_hChkForce = CreateWindowExA(0, "BUTTON", "Forçar Modo de Varredura Bruta de NAL Units (-f)",
                                          WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 25, y, 400, 25, hwnd, (HMENU)IDC_CHK_FORCE_RAW, g_hInst, NULL);
            SendMessage(g_hChkForce, WM_SETFONT, (WPARAM)hFont, TRUE);

            y += 42;
            g_hBtnStart = CreateWindowExA(0, "BUTTON", "⚡  INICIAR RESTAURAÇÃO DO VÍDEO",
                                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 25, y, 620, 42, hwnd, (HMENU)IDC_BTN_START, g_hInst, NULL);
            SendMessage(g_hBtnStart, WM_SETFONT, (WPARAM)hBoldFont, TRUE);

            y += 55;
            g_hProgress = CreateWindowExA(0, PROGRESS_CLASSA, "",
                                         WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 25, y, 620, 20, hwnd, (HMENU)IDC_PROGRESS_BAR, g_hInst, NULL);

            y += 28;
            g_hTxtStatus = CreateWindowExA(0, "STATIC", "Pronto. Selecione o vídeo corrompido para começar.",
                                          WS_CHILD | WS_VISIBLE, 25, y, 620, 25, hwnd, (HMENU)IDC_TXT_STATUS, g_hInst, NULL);
            SendMessage(g_hTxtStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

            g_hEdtLog = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                        WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                                        25, 55, 620, 415, hwnd, (HMENU)IDC_EDT_LOG, g_hInst, NULL);
            SendMessage(g_hEdtLog, WM_SETFONT, (WPARAM)hFont, TRUE);
            AppendLog("FixCFTV v1.0.0 - PladixOficial (https://github.com/pladix/FixCFTV)");
            AppendLog("Motor de restauração em C inicializado com sucesso.");

            UpdateTabVisibility();
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR pnm = (LPNMHDR)lParam;
            if (pnm->idFrom == IDC_TAB_MAIN && pnm->code == TCN_SELCHANGE) {
                g_current_tab = TabCtrl_GetCurSel(g_hTab);
                if (g_current_tab == 2) {
                    MessageBoxA(hwnd,
                                "FixCFTV v1.0.0\r\n"
                                "Desenvolvido por PladixOficial\r\n\r\n"
                                "Repositório GitHub: https://github.com/pladix/FixCFTV\r\n\r\n"
                                "Sistema Forense de Restauração de Vídeos de Segurança.\r\n"
                                "Compatível com Intelbras, Dahua, Hikvision, AITEK e streams H.264/H.265.",
                                "Sobre o FixCFTV", MB_ICONINFORMATION | MB_OK);
                    TabCtrl_SetCurSel(g_hTab, 0);
                    g_current_tab = 0;
                }
                UpdateTabVisibility();
            }
            return 0;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_BROWSE_IN:
                    OnBrowseInput(hwnd);
                    break;
                case IDC_BTN_BROWSE_OUT:
                    OnBrowseOutput(hwnd);
                    break;
                case IDC_BTN_BROWSE_REF:
                    OnBrowseRef(hwnd);
                    break;
                case IDC_BTN_START:
                    OnStartRepair(hwnd);
                    break;
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev; (void)lpCmd;
    g_hInst = hInst;

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "FixCFTVMainWndClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) return 1;

    g_hMainWnd = CreateWindowExA(WS_EX_APPWINDOW,
                                 "FixCFTVMainWndClass",
                                 "FixCFTV by PladixOficial - Restauração de Vídeos de CFTV",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 695, 545,
                                 NULL, NULL, hInst, NULL);

    if (!g_hMainWnd) return 1;

    ShowWindow(g_hMainWnd, nShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
