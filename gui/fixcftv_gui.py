#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FixCFTV - Sistema de Restauração Forense de Vídeos de CFTV / DVR / NVR
Desenvolvido por PladixOficial
Repositório Oficial: https://github.com/pladix/FixCFTV
"""

import os
import sys
import subprocess
import threading
import time
import re
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

# Ativação de High-DPI no Windows para evitar fontes borradas em qualquer escala (100%, 125%, 150%, 200%)
if sys.platform == "win32":
    try:
        import ctypes
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2) # Per-Monitor V2
        except Exception:
            ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass

BASE_DIR = Path(__file__).resolve().parent.parent
BIN_DIR = BASE_DIR

# Busca pelo executável do motor em C compilado
CLI_NAMES = ["FixCFTV.exe", "FixCFTV", "cctv_video_healer.exe", "cctv_video_healer"]
HEALER_BIN = None
for name in CLI_NAMES:
    candidate = BIN_DIR / name
    if candidate.exists():
        HEALER_BIN = candidate
        break

# Paleta de Cores Moderna e Elegante (Estilo Slate / Dark Blue / Indigo / Emerald)
PALETTE = {
    "bg": "#0f172a",          # Slate 900
    "card": "#1e293b",        # Slate 800
    "card_alt": "#182234",    # Slate 850
    "header": "#090d16",      # Slate 950
    "border": "#334155",      # Slate 700
    "input_bg": "#090d16",
    "text": "#f8fafc",        # Slate 50
    "text_muted": "#94a3b8",  # Slate 400
    "accent": "#6366f1",      # Indigo 500
    "accent_hover": "#4f46e5",
    "cyan": "#06b6d4",        # Cyan 500
    "success": "#10b981",     # Emerald 500
    "success_hover": "#059669",
    "warning": "#f59e0b",     # Amber 500
    "danger": "#ef4444",      # Rose 500
    "badge_bg": "#273549"
}

def format_bytes(bytes_val):
    if bytes_val < 0:
        return "--"
    units = ["B", "KB", "MB", "GB", "TB"]
    idx = 0
    val = float(bytes_val)
    while val >= 1024.0 and idx < 4:
        val /= 1024.0
        idx += 1
    return f"{bytes_val} B" if idx == 0 else f"{val:.2f} {units[idx]}"

class FixCFTVApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("FixCFTV - Restauração Profissional de Vídeos de CFTV | PladixOficial")
        self.geometry("1060x820")
        self.minsize(920, 680)
        self.configure(bg=PALETTE["bg"])

        # Estado do processo individual
        self.process = None
        self.is_running = False
        self.recovered_file_path = None

        # Estado da fila em massa (batch)
        self.batch_queue = [] # lista de dicts com dados de cada arquivo
        self.batch_running = False
        self.batch_cancel_flag = False

        self._setup_styles()
        self._build_top_bar()
        self._build_tabs()
        self._build_footer()

    def _setup_styles(self):
        style = ttk.Style(self)
        style.theme_use("clam")

        # Configurações globais de widgets ttk
        style.configure(".", background=PALETTE["bg"], foreground=PALETTE["text"], font=("Segoe UI", 10))

        # Abas
        style.configure("TNotebook", background=PALETTE["header"], borderwidth=0)
        style.configure("TNotebook.Tab", background=PALETTE["card"], foreground=PALETTE["text_muted"],
                        padding=[16, 9], font=("Segoe UI", 10, "bold"), borderwidth=0)
        style.map("TNotebook.Tab",
                  background=[("selected", PALETTE["accent"]), ("active", "#2d3748")],
                  foreground=[("selected", "#ffffff"), ("active", "#e2e8f0")])

        # Cards
        style.configure("Card.TFrame", background=PALETTE["card"])
        style.configure("Inner.TFrame", background=PALETTE["card_alt"])

        # Barra de Progresso
        style.configure("Accent.Horizontal.TProgressbar",
                        troughcolor=PALETTE["input_bg"],
                        background=PALETTE["accent"],
                        thickness=16)
        style.configure("Batch.Horizontal.TProgressbar",
                        troughcolor=PALETTE["input_bg"],
                        background=PALETTE["cyan"],
                        thickness=16)

        # Tabela Treeview (Fila em Massa)
        style.configure("Treeview",
                        background=PALETTE["card_alt"],
                        foreground=PALETTE["text"],
                        fieldbackground=PALETTE["input_bg"],
                        rowheight=28,
                        font=("Segoe UI", 9))
        style.configure("Treeview.Heading",
                        background=PALETTE["header"],
                        foreground=PALETTE["cyan"],
                        font=("Segoe UI", 9, "bold"),
                        padding=[8, 6])
        style.map("Treeview",
                  background=[("selected", PALETTE["accent_hover"])],
                  foreground=[("selected", "#ffffff")])

    def _build_top_bar(self):
        top_frame = tk.Frame(self, bg=PALETTE["header"], padx=20, pady=12)
        top_frame.pack(fill="x", side="top")

        left_box = tk.Frame(top_frame, bg=PALETTE["header"])
        left_box.pack(side="left")

        title_lbl = tk.Label(left_box, text="FixCFTV", font=("Segoe UI", 18, "bold"),
                             fg="#ffffff", bg=PALETTE["header"])
        title_lbl.pack(side="left")

        badge_lbl = tk.Label(left_box, text=" by PladixOficial ", font=("Segoe UI", 8, "bold"),
                             fg=PALETTE["cyan"], bg=PALETTE["badge_bg"], padx=6, pady=2)
        badge_lbl.pack(side="left", padx=(10, 0))

        sub_lbl = tk.Label(top_frame, text="Restauração Forense Autônoma de Gravações de CFTV (Intelbras, Dahua, Hikvision, AITEK, XM)",
                           font=("Segoe UI", 9), fg=PALETTE["text_muted"], bg=PALETTE["header"])
        sub_lbl.pack(side="left", padx=(16, 0), pady=(3, 0))

        btn_git = tk.Button(top_frame, text="⭐ GitHub: github.com/pladix/FixCFTV", font=("Segoe UI", 9, "bold"),
                            bg=PALETTE["badge_bg"], fg=PALETTE["text"], activebackground=PALETTE["accent"],
                            relief="flat", cursor="hand2", padx=12, pady=5,
                            command=self._open_github)
        btn_git.pack(side="right")

    def _build_tabs(self):
        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill="both", expand=True, padx=15, pady=(10, 8))

        self.tab_repair = ttk.Frame(self.notebook, style="Card.TFrame")
        self.tab_batch = ttk.Frame(self.notebook, style="Card.TFrame")
        self.tab_report = ttk.Frame(self.notebook, style="Card.TFrame")
        self.tab_inspect = ttk.Frame(self.notebook, style="Card.TFrame")
        self.tab_guide = ttk.Frame(self.notebook, style="Card.TFrame")

        self.notebook.add(self.tab_repair, text=" 📁 Recuperação Individual ")
        self.notebook.add(self.tab_batch, text=" 📚 Fila em Massa (Lote) ")
        self.notebook.add(self.tab_report, text=" 📊 Relatório & Diagnóstico ")
        self.notebook.add(self.tab_inspect, text=" 🎬 Player & Validação ")
        self.notebook.add(self.tab_guide, text=" 📹 Guia de Câmeras ")

        self._build_repair_tab()
        self._build_batch_tab()
        self._build_report_tab()
        self._build_inspect_tab()
        self._build_guide_tab()

    # --------------------------------------------------------------------------
    # ABA 1: RECUPERAÇÃO INDIVIDUAL AUTÔNOMA
    # --------------------------------------------------------------------------
    def _build_repair_tab(self):
        canvas_frame = tk.Frame(self.tab_repair, bg=PALETTE["card"], padx=20, pady=15)
        canvas_frame.pack(fill="both", expand=True)

        # 1. Card de Seleção do Arquivo Corrompido
        f_box = tk.LabelFrame(canvas_frame, text=" 1. Arquivo de Vídeo Corrompido ", font=("Segoe UI", 10, "bold"),
                              fg=PALETTE["cyan"], bg=PALETTE["card"], bd=1, relief="solid", padx=14, pady=10)
        f_box.pack(fill="x", pady=(0, 10))

        tk.Label(f_box, text="Vídeo quebrado da câmera de segurança (Queda de energia, MP4/MOV sem átomo moov, DAT, DAV):",
                 font=("Segoe UI", 9, "bold"), fg=PALETTE["text"], bg=PALETTE["card"]).grid(row=0, column=0, sticky="w")
        
        self.txt_in = tk.Entry(f_box, bg=PALETTE["input_bg"], fg=PALETTE["text"],
                               insertbackground=PALETTE["text"], font=("Consolas", 10), bd=1, relief="solid")
        self.txt_in.grid(row=1, column=0, sticky="ew", padx=(0, 8), pady=(3, 8))
        
        btn_in = tk.Button(f_box, text="📂 Selecionar Vídeo Quebrado...", bg=PALETTE["badge_bg"], fg=PALETTE["text"],
                           activebackground=PALETTE["accent"], relief="flat", padx=14, pady=4, cursor="hand2",
                           command=self._select_input)
        btn_in.grid(row=1, column=1, pady=(3, 8))

        tk.Label(f_box, text="Destino do Vídeo Restaurado (Novo contêiner MP4 compatível com FastStart):",
                 font=("Segoe UI", 9, "bold"), fg=PALETTE["text"], bg=PALETTE["card"]).grid(row=2, column=0, sticky="w")
        
        self.txt_out = tk.Entry(f_box, bg=PALETTE["input_bg"], fg=PALETTE["text"],
                                insertbackground=PALETTE["text"], font=("Consolas", 10), bd=1, relief="solid")
        self.txt_out.grid(row=3, column=0, sticky="ew", padx=(0, 8), pady=(3, 4))
        
        btn_out = tk.Button(f_box, text="💾 Salvar em...", bg=PALETTE["badge_bg"], fg=PALETTE["text"],
                            activebackground=PALETTE["accent"], relief="flat", padx=14, pady=4, cursor="hand2",
                            command=self._select_output)
        btn_out.grid(row=3, column=1, pady=(3, 4))

        f_box.columnconfigure(0, weight=1)

        # 2. Card de Diagnóstico Forense Automático em Tempo Real
        self.diag_box = tk.LabelFrame(canvas_frame, text=" 2. Diagnóstico Automático & Cascata Inteligente ",
                                      font=("Segoe UI", 10, "bold"), fg=PALETTE["cyan"], bg=PALETTE["card"],
                                      bd=1, relief="solid", padx=14, pady=10)
        self.diag_box.pack(fill="x", pady=(0, 10))

        self.lbl_diag_status = tk.Label(self.diag_box,
            text="⚡ Modo 100% Autônomo Ativo: Selecione o arquivo danificado acima e o FixCFTV testará automaticamente múltiplos métodos internos em cascata (Container Permissivo, Varredura NAL, AVCC e DHAV), sem necessidade de vídeo original.",
            justify="left", font=("Segoe UI", 9), fg=PALETTE["text"], bg=PALETTE["card"], wraplength=980)
        self.lbl_diag_status.pack(anchor="w", pady=(0, 6))

        # 3. Card de Vídeo de Referência (Fallback / Opcional)
        self.ref_box = tk.LabelFrame(canvas_frame, text=" 3. Vídeo de Referência (Opcional / Recurso de Transplante) ",
                                     font=("Segoe UI", 10, "bold"), fg=PALETTE["warning"], bg=PALETTE["card"],
                                     bd=1, relief="solid", padx=14, pady=8)
        self.ref_box.pack(fill="x", pady=(0, 10))

        tk.Label(self.ref_box,
            text="💡 O FixCFTV recupera vídeos automaticamente sem referência. Caso todos os métodos internos autônomos se esgotem (quando a câmera não emitiu nenhum cabeçalho óptico), você pode selecionar um vídeo bom de poucos segundos da mesma câmera para transplante de metadados:",
            justify="left", font=("Segoe UI", 8), fg=PALETTE["text_muted"], bg=PALETTE["card"], wraplength=980).pack(anchor="w", pady=(0, 4))

        ref_row = tk.Frame(self.ref_box, bg=PALETTE["card"])
        ref_row.pack(fill="x")

        self.txt_ref = tk.Entry(ref_row, bg=PALETTE["input_bg"], fg=PALETTE["text"],
                                insertbackground=PALETTE["text"], font=("Consolas", 10), bd=1, relief="solid")
        self.txt_ref.pack(side="left", fill="x", expand=True, padx=(0, 8))

        btn_ref = tk.Button(ref_row, text="📂 Selecionar Vídeo Bom...", bg=PALETTE["badge_bg"], fg=PALETTE["text"],
                            activebackground=PALETTE["accent"], relief="flat", padx=12, pady=3, cursor="hand2",
                            command=self._select_ref)
        btn_ref.pack(side="right")

        # Opções complementares
        opt_bar = tk.Frame(canvas_frame, bg=PALETTE["card"])
        opt_bar.pack(fill="x", pady=(0, 8))

        self.var_force = tk.BooleanVar(value=False)
        chk_f = tk.Checkbutton(opt_bar, text="Forçar Varredura Bruta Direta (-f)", variable=self.var_force,
                               fg=PALETTE["text"], bg=PALETTE["card"], selectcolor=PALETTE["input_bg"],
                               activebackground=PALETTE["card"], font=("Segoe UI", 9))
        chk_f.pack(side="left", padx=(0, 20))

        tk.Label(opt_bar, text="Taxa Padrão (FPS):", fg=PALETTE["text_muted"], bg=PALETTE["card"]).pack(side="left", padx=(0, 5))
        self.combo_fps = ttk.Combobox(opt_bar, values=["Automático (Padrão 25)", "30 fps", "25 fps", "20 fps", "15 fps", "10 fps"],
                                      width=20, state="readonly")
        self.combo_fps.current(0)
        self.combo_fps.pack(side="left")

        # 4. Botões de Execução
        act_box = tk.Frame(canvas_frame, bg=PALETTE["card"], pady=6)
        act_box.pack(fill="x")

        self.btn_run = tk.Button(act_box, text="⚡  INICIAR RESTAURAÇÃO AUTÔNOMA", font=("Segoe UI", 12, "bold"),
                                 bg=PALETTE["success"], fg="#ffffff", activebackground=PALETTE["success_hover"],
                                 relief="flat", cursor="hand2", padx=24, pady=12, command=self._start_repair)
        self.btn_run.pack(side="left", fill="x", expand=True, padx=(0, 10))

        self.btn_abort = tk.Button(act_box, text="⏹️ Cancelar", font=("Segoe UI", 11),
                                   bg="#475569", fg="#ffffff", activebackground="#334155",
                                   relief="flat", state="disabled", padx=18, pady=12, command=self._abort_repair)
        self.btn_abort.pack(side="right")

        # Barra de Progresso e Status
        self.pbar = ttk.Progressbar(canvas_frame, mode="indeterminate", style="Accent.Horizontal.TProgressbar")
        self.pbar.pack(fill="x", pady=(10, 4))

        self.lbl_progress_info = tk.Label(canvas_frame, text="Pronto para restaurar. Selecione o vídeo e clique em Iniciar.",
                                          font=("Segoe UI", 9), fg=PALETTE["text_muted"], bg=PALETTE["card"])
        self.lbl_progress_info.pack(anchor="w")

    # --------------------------------------------------------------------------
    # ABA 2: FILA EM MASSA (BATCH PROCESSING / LOTE SEM LIMITES)
    # --------------------------------------------------------------------------
    def _build_batch_tab(self):
        box = tk.Frame(self.tab_batch, bg=PALETTE["card"], padx=18, pady=15)
        box.pack(fill="both", expand=True)

        # Painel de controle superior da fila
        ctrl_bar = tk.Frame(box, bg=PALETTE["card"])
        ctrl_bar.pack(fill="x", pady=(0, 10))

        btn_add_files = tk.Button(ctrl_bar, text="➕ Adicionar Vídeos...", font=("Segoe UI", 9, "bold"),
                                  bg=PALETTE["badge_bg"], fg=PALETTE["text"], activebackground=PALETTE["accent"],
                                  relief="flat", cursor="hand2", padx=12, pady=6, command=self._batch_add_files)
        btn_add_files.pack(side="left", padx=(0, 8))

        btn_add_dir = tk.Button(ctrl_bar, text="📁 Adicionar Pasta Inteira...", font=("Segoe UI", 9, "bold"),
                                bg=PALETTE["badge_bg"], fg=PALETTE["text"], activebackground=PALETTE["accent"],
                                relief="flat", cursor="hand2", padx=12, pady=6, command=self._batch_add_folder)
        btn_add_dir.pack(side="left", padx=(0, 8))

        btn_del = tk.Button(ctrl_bar, text="🗑️ Remover Selecionado", font=("Segoe UI", 9),
                            bg=PALETTE["card_alt"], fg=PALETTE["text_muted"], activebackground=PALETTE["danger"],
                            relief="flat", cursor="hand2", padx=10, pady=6, command=self._batch_remove_selected)
        btn_del.pack(side="left", padx=(0, 8))

        btn_clear = tk.Button(ctrl_bar, text="🧹 Limpar Fila", font=("Segoe UI", 9),
                              bg=PALETTE["card_alt"], fg=PALETTE["text_muted"], activebackground=PALETTE["danger"],
                              relief="flat", cursor="hand2", padx=10, pady=6, command=self._batch_clear)
        btn_clear.pack(side="left")

        self.lbl_batch_count = tk.Label(ctrl_bar, text="0 vídeos na fila", font=("Segoe UI", 9, "bold"),
                                        fg=PALETTE["cyan"], bg=PALETTE["card"])
        self.lbl_batch_count.pack(side="right", padx=(0, 5))

        # Linha de Pasta de Saída do Lote
        out_bar = tk.Frame(box, bg=PALETTE["card_alt"], padx=10, pady=8)
        out_bar.pack(fill="x", pady=(0, 10))

        tk.Label(out_bar, text="Salvar Todos os Vídeos Recuperados em:", font=("Segoe UI", 9, "bold"),
                 fg=PALETTE["text"], bg=PALETTE["card_alt"]).pack(side="left", padx=(0, 10))

        self.txt_batch_out = tk.Entry(out_bar, bg=PALETTE["input_bg"], fg=PALETTE["text"],
                                      insertbackground=PALETTE["text"], font=("Consolas", 9), bd=1, relief="solid")
        self.txt_batch_out.pack(side="left", fill="x", expand=True, padx=(0, 8))

        btn_browse_batch_out = tk.Button(out_bar, text="📂 Escolher Pasta...", font=("Segoe UI", 8),
                                         bg=PALETTE["badge_bg"], fg=PALETTE["text"], activebackground=PALETTE["accent"],
                                         relief="flat", cursor="hand2", padx=10, pady=3, command=self._select_batch_output_dir)
        btn_browse_batch_out.pack(side="right")

        # Tabela Treeview com os vídeos da fila
        cols = ("num", "name", "size", "status", "method", "frames", "res", "time")
        self.tree_batch = ttk.Treeview(box, columns=cols, show="headings", selectmode="browse")

        self.tree_batch.heading("num", text="#")
        self.tree_batch.heading("name", text="Arquivo Corrompido")
        self.tree_batch.heading("size", text="Tamanho")
        self.tree_batch.heading("status", text="Status")
        self.tree_batch.heading("method", text="Método Utilizado")
        self.tree_batch.heading("frames", text="Quadros")
        self.tree_batch.heading("res", text="Resolução")
        self.tree_batch.heading("time", text="Tempo")

        self.tree_batch.column("num", width=40, anchor="center")
        self.tree_batch.column("name", width=340, anchor="w")
        self.tree_batch.column("size", width=90, anchor="center")
        self.tree_batch.column("status", width=140, anchor="center")
        self.tree_batch.column("method", width=220, anchor="w")
        self.tree_batch.column("frames", width=80, anchor="center")
        self.tree_batch.column("res", width=90, anchor="center")
        self.tree_batch.column("time", width=70, anchor="center")

        # Tags de cores para o status
        self.tree_batch.tag_configure("pending", foreground=PALETTE["text_muted"])
        self.tree_batch.tag_configure("running", foreground=PALETTE["warning"])
        self.tree_batch.tag_configure("success", foreground=PALETTE["success"])
        self.tree_batch.tag_configure("needs_ref", foreground=PALETTE["warning"])
        self.tree_batch.tag_configure("failed", foreground=PALETTE["danger"])

        # Scrollbar da tabela
        scroll_y = ttk.Scrollbar(box, orient="vertical", command=self.tree_batch.yview)
        self.tree_batch.configure(yscrollcommand=scroll_y.set)
        
        self.tree_batch.pack(side="top", fill="both", expand=True)
        scroll_y.pack(side="right", fill="y", before=self.tree_batch)

        self.tree_batch.bind("<Double-1>", self._on_tree_double_click)

        # Controles inferiores de execução da fila em massa
        batch_act = tk.Frame(box, bg=PALETTE["card"], pady=10)
        batch_act.pack(fill="x")

        self.btn_batch_start = tk.Button(batch_act, text="⚡  INICIAR RESTAURAÇÃO EM MASSA (FILA COMPLETA)",
                                         font=("Segoe UI", 12, "bold"), bg=PALETTE["cyan"], fg="#000000",
                                         activebackground="#0891b2", relief="flat", cursor="hand2", padx=20, pady=10,
                                         command=self._start_batch_processing)
        self.btn_batch_start.pack(side="left", fill="x", expand=True, padx=(0, 10))

        self.btn_batch_ref_item = tk.Button(batch_act, text="🎯 Definir Referência para Selecionado",
                                            font=("Segoe UI", 9, "bold"), bg=PALETTE["badge_bg"], fg=PALETTE["text"],
                                            activebackground=PALETTE["accent"], relief="flat", cursor="hand2", padx=12, pady=10,
                                            command=self._set_ref_for_selected)
        self.btn_batch_ref_item.pack(side="left", padx=(0, 10))

        self.btn_batch_cancel = tk.Button(batch_act, text="⏹️ Interromper Lote", font=("Segoe UI", 10),
                                          bg="#475569", fg="#ffffff", activebackground="#334155",
                                          relief="flat", state="disabled", padx=16, pady=10, command=self._cancel_batch)
        self.btn_batch_cancel.pack(side="right")

        # Barra de Progresso do Lote
        self.batch_pbar = ttk.Progressbar(box, mode="determinate", style="Batch.Horizontal.TProgressbar")
        self.batch_pbar.pack(fill="x", pady=(4, 2))

        self.lbl_batch_status = tk.Label(box, text="Fila pronta. Adicione arquivos ou pastas e clique em Iniciar Restauração em Massa.",
                                         font=("Segoe UI", 9), fg=PALETTE["text_muted"], bg=PALETTE["card"])
        self.lbl_batch_status.pack(anchor="w")

    # --------------------------------------------------------------------------
    # ABA 3: RELATÓRIO FORENSE & LOGS
    # --------------------------------------------------------------------------
    def _build_report_tab(self):
        box = tk.Frame(self.tab_report, bg=PALETTE["card"], padx=15, pady=15)
        box.pack(fill="both", expand=True)

        m_frame = tk.LabelFrame(box, text=" Diagnóstico Forense da Gravação ", font=("Segoe UI", 10, "bold"),
                                fg=PALETTE["cyan"], bg=PALETTE["card"], bd=1, relief="solid", padx=12, pady=10)
        m_frame.pack(fill="x", pady=(0, 10))

        self.card_codec = tk.Label(m_frame, text="Codec: --", font=("Segoe UI", 9, "bold"), fg=PALETTE["text"], bg=PALETTE["card"])
        self.card_codec.grid(row=0, column=0, sticky="w", padx=10, pady=4)

        self.card_res = tk.Label(m_frame, text="Resolução: --", font=("Segoe UI", 9, "bold"), fg=PALETTE["text"], bg=PALETTE["card"])
        self.card_res.grid(row=0, column=1, sticky="w", padx=10, pady=4)

        self.card_frames = tk.Label(m_frame, text="Quadros Restaurados: --", font=("Segoe UI", 9, "bold"), fg=PALETTE["text"], bg=PALETTE["card"])
        self.card_frames.grid(row=1, column=0, sticky="w", padx=10, pady=4)

        self.card_speed = tk.Label(m_frame, text="Vazão / Throughput: --", font=("Segoe UI", 9, "bold"), fg=PALETTE["text"], bg=PALETTE["card"])
        self.card_speed.grid(row=1, column=1, sticky="w", padx=10, pady=4)

        m_frame.columnconfigure(0, weight=1)
        m_frame.columnconfigure(1, weight=1)

        log_header = tk.Frame(box, bg=PALETTE["card"])
        log_header.pack(fill="x", pady=(6, 2))
        tk.Label(log_header, text="Log Técnico de Execução Forense (Motor C):", font=("Segoe UI", 9, "bold"),
                 fg=PALETTE["text"], bg=PALETTE["card"]).pack(side="left")

        btn_export = tk.Button(log_header, text="💾 Salvar Relatório TXT", bg=PALETTE["badge_bg"], fg=PALETTE["text"],
                               activebackground=PALETTE["accent"], relief="flat", font=("Segoe UI", 8), command=self._export_log)
        btn_export.pack(side="right", padx=(5, 0))

        btn_copy = tk.Button(log_header, text="📋 Copiar", bg=PALETTE["badge_bg"], fg=PALETTE["text"],
                             activebackground=PALETTE["accent"], relief="flat", font=("Segoe UI", 8), command=self._copy_log)
        btn_copy.pack(side="right", padx=(5, 0))

        btn_clr = tk.Button(log_header, text="🗑️ Limpar", bg=PALETTE["badge_bg"], fg=PALETTE["text"],
                            activebackground=PALETTE["accent"], relief="flat", font=("Segoe UI", 8), command=self._clear_log)
        btn_clr.pack(side="right")

        self.txt_log = tk.Text(box, bg=PALETTE["input_bg"], fg="#a6adc8", font=("Consolas", 9),
                               insertbackground="#cdd6f4", bd=1, relief="solid", wrap="word")
        self.txt_log.pack(fill="both", expand=True)

    # --------------------------------------------------------------------------
    # ABA 4: PLAYER & VALIDAÇÃO
    # --------------------------------------------------------------------------
    def _build_inspect_tab(self):
        box = tk.Frame(self.tab_inspect, bg=PALETTE["card"], padx=20, pady=20)
        box.pack(fill="both", expand=True)

        tk.Label(box, text="Validação de Integridade e Reprodução do Vídeo Restaurado", font=("Segoe UI", 12, "bold"),
                 fg=PALETTE["cyan"], bg=PALETTE["card"]).pack(anchor="w", pady=(0, 8))

        tk.Label(box, text="Certifique-se de que a gravação restaurada foi reconstruída corretamente e é capaz\n"
                           "de ser reproduzida perfeitamente em qualquer equipamento sem travamentos.",
                 justify="left", font=("Segoe UI", 9), fg=PALETTE["text_muted"], bg=PALETTE["card"]).pack(anchor="w", pady=(0, 15))

        btn_bar = tk.Frame(box, bg=PALETTE["card"])
        btn_bar.pack(fill="x", pady=6)

        self.btn_play = tk.Button(btn_bar, text="▶️  Reproduzir Vídeo no Player Padrão", font=("Segoe UI", 11, "bold"),
                                  bg=PALETTE["accent"], fg="#ffffff", activebackground=PALETTE["accent_hover"],
                                  relief="flat", cursor="hand2", padx=16, pady=10, command=self._play_recovered)
        self.btn_play.pack(side="left", padx=(0, 12))

        self.btn_ffprobe = tk.Button(btn_bar, text="🔍  Verificar Decodificação com FFprobe / FFmpeg", font=("Segoe UI", 11),
                                     bg=PALETTE["badge_bg"], fg=PALETTE["text"], activebackground=PALETTE["accent"],
                                     relief="flat", cursor="hand2", padx=16, pady=10, command=self._inspect_video)
        self.btn_ffprobe.pack(side="left")

        self.txt_inspect = tk.Text(box, bg=PALETTE["input_bg"], fg="#a6adc8", font=("Consolas", 9),
                                   insertbackground="#cdd6f4", bd=1, relief="solid", wrap="word")
        self.txt_inspect.pack(fill="both", expand=True, pady=(15, 0))

    # --------------------------------------------------------------------------
    # ABA 5: GUIA DE CÂMERAS & CFTV
    # --------------------------------------------------------------------------
    def _build_guide_tab(self):
        box = tk.Frame(self.tab_guide, bg=PALETTE["card"], padx=20, pady=15)
        box.pack(fill="both", expand=True)

        guide_txt = tk.Text(box, bg=PALETTE["card_alt"], fg="#e2e8f0", font=("Segoe UI", 10),
                            relief="solid", bd=1, padx=16, pady=16, wrap="word")
        guide_txt.pack(fill="both", expand=True)

        content = """GUIA TÉCNICO: RECUPERAÇÃO FORENSE DE GRAVAÇÕES DE CFTV / DVR / NVR

1. O QUE ACONTECE NA QUEDA DE ENERGIA?
   • Gravadores de CFTV (DVRs e NVRs) gravam o fluxo de dados brutos de vídeo no bloco 'mdat' do arquivo MP4/MOV em tempo real.
   • O átomo de cabeçalho 'moov' (que contém os índices de cada segundo gravado e o dicionário do sensor) só é salvo quando o arquivo é devidamente encerrado pelo sistema operacional.
   • Uma queda abrupta de luz ou desligamento força a interrupção da gravação antes de salvar o 'moov', gerando um arquivo corrompido que computadores e players comuns rejeitam como inválido.

2. CASCATA INTELIGENTE DE RECUPERAÇÃO DO FIXCFTV:
   • O FixCFTV implementa um motor de baixo nível em Linguagem C que varre o payload de vídeo e tenta restaurar os quadros através de 5 estratégias sequenciais autônomas:
     1. Re-indexação Permissiva de Contêiner com tolerância a pacotes quebrados.
     2. Varredura Bruta de Unidades NAL (H.264 / H.265) e extração de SPS/PPS embutidos.
     3. Desempacotamento de blocos AVCC com prefixos de comprimento de 4 bytes.
     4. Desencapsulador Dahua / Intelbras DHAV (.dav ou tags proprietárias).
     5. Injeção de parâmetros SPS/PPS sintéticos heurísticos quando o cabeçalho original é perdido.

3. QUANDO É NECESSÁRIO O VÍDEO DE REFERÊNCIA?
   • Em cerca de 90% dos casos, o FixCFTV recupera o vídeo de forma 100% autônoma.
   • Se e somente se todos os métodos autônomos internos falharem (quando a câmera perdeu completamente as configurações do sensor óptico), o FixCFTV solicitará um arquivo funcional gravado pela mesma câmera para clonar os parâmetros e salvar o vídeo.

4. SEM LIMITES DE TAMANHO OU QUANTIDADE:
   • O FixCFTV utiliza processamento em fluxo contínuo (streaming em blocos de 1 MB e offsets de 64-bit).
   • O consumo de memória RAM permanece constante em menos de 25 MB, seja processando um arquivo de 10 MB ou imagens de disco de 500 GB / 2 TB de DVRs.
   • Na aba 'Fila em Massa', você pode adicionar quantos arquivos ou pastas desejar para restauração simultânea.

Projeto no GitHub: https://github.com/pladix/FixCFTV
Desenvolvido por PladixOficial
"""
        guide_txt.insert("1.0", content)
        guide_txt.config(state="disabled")

    def _build_footer(self):
        footer = tk.Frame(self, bg=PALETTE["header"], padx=15, pady=6)
        footer.pack(fill="x", side="bottom")

        self.lbl_status = tk.Label(footer, text="Pronto para restaurar vídeos de CFTV.",
                                   font=("Segoe UI", 9), fg=PALETTE["text_muted"], bg=PALETTE["header"])
        self.lbl_status.pack(side="left")

        engine_lbl = tk.Label(footer, text="FixCFTV Engine: C99 / FFmpeg Core | github.com/pladix/FixCFTV",
                              font=("Segoe UI", 9), fg=PALETTE["text_muted"], bg=PALETTE["header"])
        engine_lbl.pack(side="right")

    # --------------------------------------------------------------------------
    # MÉTODOS DE CONTROLE DA ABA 1 (INDIVIDUAL)
    # --------------------------------------------------------------------------
    def _select_input(self):
        path = filedialog.askopenfilename(
            title="Selecione o vídeo corrompido de CFTV",
            filetypes=[("Vídeos Suportados (*.mp4, *.mov, *.dat, *.dav, *.264, *.ts)", "*.mp4 *.mov *.dat *.dav *.264 *.h264 *.ts *.raw *.avi"),
                       ("Todos os arquivos (*.*)", "*.*")]
        )
        if path:
            self.txt_in.delete(0, tk.END)
            self.txt_in.insert(0, path)

            p = Path(path)
            auto_out = p.parent / f"{p.stem}_recuperado.mp4"
            self.txt_out.delete(0, tk.END)
            self.txt_out.insert(0, str(auto_out))

            # Atualização do diagnóstico forense na interface
            size_bytes = p.stat().st_size if p.exists() else 0
            size_fmt = format_bytes(size_bytes)
            self.lbl_diag_status.config(
                text=f"🔍 Arquivo Selecionado: {p.name} ({size_fmt})\n"
                     f"O FixCFTV analisará automaticamente a causa da falha (moov truncado, payload mdat, DHAV, AVCC) "
                     f"e executará a cascata autônoma de recuperação sem necessidade inicial de vídeo de referência."
            )

    def _select_output(self):
        path = filedialog.asksaveasfilename(
            title="Salvar vídeo recuperado",
            defaultextension=".mp4",
            filetypes=[("Vídeo MP4 (*.mp4)", "*.mp4"), ("Todos os arquivos (*.*)", "*.*")]
        )
        if path:
            self.txt_out.delete(0, tk.END)
            self.txt_out.insert(0, path)

    def _select_ref(self):
        path = filedialog.askopenfilename(
            title="Selecione o vídeo íntegro de referência",
            filetypes=[("Vídeo MP4/MOV (*.mp4, *.mov)", "*.mp4 *.mov"), ("Todos os arquivos (*.*)", "*.*")]
        )
        if path:
            self.txt_ref.delete(0, tk.END)
            self.txt_ref.insert(0, path)

    def _start_repair(self):
        in_p = self.txt_in.get().strip()
        out_p = self.txt_out.get().strip()
        ref_p = self.txt_ref.get().strip()

        if not in_p:
            messagebox.showwarning("Aviso", "Por favor, selecione o arquivo de vídeo corrompido.")
            return

        if not out_p:
            messagebox.showwarning("Aviso", "Por favor, selecione o caminho de destino do arquivo restaurado.")
            return

        if not HEALER_BIN or not HEALER_BIN.exists():
            messagebox.showerror("Binário não encontrado",
                                 f"O executável do motor C '{HEALER_BIN}' não foi localizado.\n"
                                 "Compile o projeto com 'make' ou verifique a pasta raiz.")
            return

        cmd = [str(HEALER_BIN), "-i", in_p, "-o", out_p]

        if ref_p:
            cmd.extend(["-r", ref_p])

        if self.var_force.get():
            cmd.append("-f")

        fps_str = self.combo_fps.get()
        m = re.search(r"(\d+)", fps_str)
        if m:
            cmd.extend(["--fps", m.group(1)])

        cmd.append("-v")

        self.recovered_file_path = out_p
        self.is_running = True
        self.btn_run.config(state="disabled")
        self.btn_abort.config(state="normal")
        self.pbar.config(mode="indeterminate")
        self.pbar.start(10)

        self.lbl_status.config(text="Reconstruindo vídeo de forma autônoma...", fg=PALETTE["cyan"])
        self.lbl_progress_info.config(text="Executando perícia forense e cascata inteligente de métodos internos...")
        self.txt_log.delete("1.0", tk.END)
        self.txt_log.insert(tk.END, f"[INICIANDO] Executando comando:\n{' '.join(cmd)}\n\n")

        threading.Thread(target=self._worker_thread, args=(cmd,), daemon=True).start()

    def _worker_thread(self, cmd):
        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
            )

            for line in self.process.stdout:
                self.after(0, self._handle_log_line, line)

            self.process.wait()
            code = self.process.returncode
            self.after(0, self._on_finish, code)

        except Exception as e:
            self.after(0, self._handle_log_line, f"\n[ERRO] {str(e)}\n")
            self.after(0, self._on_finish, -1)

    def _handle_log_line(self, line):
        self.txt_log.insert(tk.END, line)
        self.txt_log.see(tk.END)

        if "Codec Identified:" in line or "Codec Identificado:" in line:
            self.card_codec.config(text=f"Codec: {line.split(':', 1)[1].strip()}")
        elif "Resolution:" in line or "Resolução:" in line:
            self.card_res.config(text=f"Resolução: {line.split(':', 1)[1].strip()}")
        elif "Recovered Frames:" in line or "Quadros Recuperados:" in line:
            self.card_frames.config(text=f"Quadros: {line.split(':', 1)[1].strip()}")
        elif "Throughput:" in line or "Vazão de Leitura:" in line:
            self.card_speed.config(text=f"Vazão: {line.split(':', 1)[1].strip()}")

    def _on_finish(self, code):
        self.is_running = False
        self.pbar.stop()
        self.pbar.config(mode="determinate", value=100)
        self.btn_run.config(state="normal")
        self.btn_abort.config(state="disabled")

        if code == 0:
            self.lbl_status.config(text="✅ Restauração concluída com sucesso!", fg=PALETTE["success"])
            self.lbl_progress_info.config(text="Novo contêiner MP4 gerado com metadados FastStart (100% concluído).")
            messagebox.showinfo("Sucesso", "Restauração finalizada com sucesso!\nO arquivo reconstruído já pode ser reproduzido.")
            self.notebook.select(2) # Aba Relatório
        elif code == 2:
            self.lbl_status.config(text="⏳ Métodos autônomos esgotados. Vídeo de referência necessário.", fg=PALETTE["warning"])
            self.lbl_progress_info.config(text="O arquivo perdeu as tabelas do sensor. Forneça um vídeo funcional da mesma câmera.")
            # Destaca o campo de referência
            self.ref_box.config(fg="#ef4444")
            resposta = messagebox.askyesno("Vídeo de Referência Necessário",
                "Todos os 5 métodos internos de recuperação autônoma foram executados sem sucesso.\n\n"
                "O arquivo perdeu completamente a descrição de parâmetros do sensor óptico da câmera.\n\n"
                "Deseja selecionar agora um vídeo funcional de referência gravado pelo mesmo DVR/canal para clonar os metadados?")
            if resposta:
                self._select_ref()
        else:
            self.lbl_status.config(text="❌ Falha na recuperação de quadros.", fg=PALETTE["danger"])
            self.lbl_progress_info.config(text="Não foram encontrados dados decodificáveis neste arquivo. Verifique a aba de Logs.")
            messagebox.showerror("Erro", "Não foi possível recuperar os quadros deste arquivo.\nConsulte a aba 'Relatório & Diagnóstico' para detalhes técnicos.")

    def _abort_repair(self):
        if self.process and self.is_running:
            try:
                self.process.terminate()
                self._handle_log_line("\n[PROCESSO CANCELADO PELO USUÁRIO]\n")
                self.lbl_status.config(text="Cancelado.", fg=PALETTE["warning"])
            except Exception:
                pass

    # --------------------------------------------------------------------------
    # MÉTODOS DE CONTROLE DA ABA 2 (FILA EM MASSA / BATCH)
    # --------------------------------------------------------------------------
    def _batch_add_files(self):
        files = filedialog.askopenfilenames(
            title="Selecione os vídeos corrompidos para processamento em massa",
            filetypes=[("Vídeos Suportados (*.mp4, *.mov, *.dat, *.dav, *.264, *.ts)", "*.mp4 *.mov *.dat *.dav *.264 *.h264 *.ts *.raw *.avi"),
                       ("Todos os arquivos (*.*)", "*.*")]
        )
        if files:
            for f in files:
                self._add_to_queue(f)
            self._refresh_batch_count()

    def _batch_add_folder(self):
        folder = filedialog.askdirectory(title="Selecione a pasta contendo gravações corrompidas")
        if folder:
            valid_exts = {".mp4", ".mov", ".dat", ".dav", ".264", ".h264", ".ts", ".raw", ".avi"}
            added = 0
            for root, _, files in os.walk(folder):
                for f in files:
                    ext = os.path.splitext(f)[1].lower()
                    if ext in valid_exts and "_recuperado.mp4" not in f:
                        full_p = os.path.join(root, f)
                        self._add_to_queue(full_p)
                        added += 1
            self._refresh_batch_count()
            if added == 0:
                messagebox.showinfo("Aviso", "Nenhum vídeo compatível foi encontrado na pasta selecionada.")

    def _add_to_queue(self, file_path):
        # Verifica duplicatas
        for item in self.batch_queue:
            if item["path"] == file_path:
                return

        p = Path(file_path)
        size_bytes = p.stat().st_size if p.exists() else 0
        idx = len(self.batch_queue) + 1
        item = {
            "id": idx,
            "path": file_path,
            "filename": p.name,
            "size": size_bytes,
            "status": "⏳ Pendente",
            "method": "--",
            "frames": "--",
            "res": "--",
            "time": "--",
            "ref": "",
            "tag": "pending"
        }
        self.batch_queue.append(item)
        self.tree_batch.insert("", "end", iid=str(idx), values=(
            idx, item["filename"], format_bytes(size_bytes), item["status"],
            item["method"], item["frames"], item["res"], item["time"]
        ), tags=(item["tag"],))

        # Define pasta de saída padrão se ainda não definida
        if not self.txt_batch_out.get().strip():
            auto_out_dir = str(p.parent / "videos_recuperados")
            self.txt_batch_out.delete(0, tk.END)
            self.txt_batch_out.insert(0, auto_out_dir)

    def _batch_remove_selected(self):
        selected = self.tree_batch.selection()
        if not selected:
            return
        idx_str = selected[0]
        self.tree_batch.delete(idx_str)
        self.batch_queue = [x for x in self.batch_queue if str(x["id"]) != idx_str]
        self._refresh_batch_count()

    def _batch_clear(self):
        self.tree_batch.delete(*self.tree_batch.get_children())
        self.batch_queue.clear()
        self._refresh_batch_count()

    def _refresh_batch_count(self):
        total = len(self.batch_queue)
        self.lbl_batch_count.config(text=f"{total} vídeos na fila")

    def _select_batch_output_dir(self):
        folder = filedialog.askdirectory(title="Selecione a pasta de saída para os vídeos recuperados")
        if folder:
            self.txt_batch_out.delete(0, tk.END)
            self.txt_batch_out.insert(0, folder)

    def _set_ref_for_selected(self):
        selected = self.tree_batch.selection()
        if not selected:
            messagebox.showinfo("Aviso", "Selecione um arquivo na tabela para atribuir o vídeo de referência.")
            return

        ref_file = filedialog.askopenfilename(
            title="Selecione o vídeo íntegro de referência para este item",
            filetypes=[("Vídeo MP4/MOV (*.mp4, *.mov)", "*.mp4 *.mov"), ("Todos os arquivos (*.*)", "*.*")]
        )
        if ref_file:
            idx_str = selected[0]
            for item in self.batch_queue:
                if str(item["id"]) == idx_str:
                    item["ref"] = ref_file
                    item["status"] = "⏳ Com Referência"
                    self.tree_batch.item(idx_str, values=(
                        item["id"], item["filename"], format_bytes(item["size"]),
                        item["status"], item["method"], item["frames"], item["res"], item["time"]
                    ), tags=("pending",))
                    messagebox.showinfo("Referência Definida", f"Referência atribuída para '{item['filename']}':\n{ref_file}")
                    break

    def _start_batch_processing(self):
        if not self.batch_queue:
            messagebox.showwarning("Aviso", "A fila em massa está vazia. Adicione vídeos ou pastas primeiro.")
            return

        out_dir = self.txt_batch_out.get().strip()
        if not out_dir:
            messagebox.showwarning("Aviso", "Por favor, selecione a pasta de destino para os vídeos recuperados.")
            return

        os.makedirs(out_dir, exist_ok=True)

        if not HEALER_BIN or not HEALER_BIN.exists():
            messagebox.showerror("Erro", f"Executável do motor C '{HEALER_BIN}' não localizado.")
            return

        self.batch_running = True
        self.batch_cancel_flag = False
        self.btn_batch_start.config(state="disabled")
        self.btn_batch_cancel.config(state="normal")
        self.batch_pbar.config(maximum=len(self.batch_queue), value=0)

        threading.Thread(target=self._batch_worker, args=(out_dir,), daemon=True).start()

    def _batch_worker(self, out_dir):
        total = len(self.batch_queue)
        success_count = 0
        needs_ref_count = 0
        fail_count = 0

        for i, item in enumerate(self.batch_queue):
            if self.batch_cancel_flag:
                break

            iid = str(item["id"])
            item["status"] = "⚙️ Processando..."
            item["tag"] = "running"
            self.after(0, self._update_tree_item, iid, item)
            self.after(0, lambda idx=i: self.lbl_batch_status.config(
                text=f"Processando vídeo {idx + 1} de {total}: {item['filename']}..."
            ))

            p_in = item["path"]
            stem = Path(p_in).stem
            p_out = os.path.join(out_dir, f"{stem}_recuperado.mp4")

            cmd = [str(HEALER_BIN), "-i", p_in, "-o", p_out]
            if item.get("ref"):
                cmd.extend(["-r", item["ref"]])

            t0 = time.time()
            try:
                proc = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
                )
                elapsed = time.time() - t0
                item["time"] = f"{elapsed:.1f}s"

                stdout_text = proc.stdout + proc.stderr

                # Extrai informações dos logs
                m_frames = re.search(r"Quadros Recuperados:\s*(\d+)", stdout_text)
                if m_frames:
                    item["frames"] = m_frames.group(1)

                m_res = re.search(r"Resolução:\s*(\d+\s*x\s*\d+)", stdout_text)
                if m_res:
                    item["res"] = m_res.group(1)

                m_method = re.search(r"Método Utilizado:\s*(.+)", stdout_text)
                if m_method:
                    item["method"] = m_method.group(1).strip()

                if proc.returncode == 0:
                    item["status"] = "✅ Sucesso"
                    item["tag"] = "success"
                    item["out_path"] = p_out
                    success_count += 1
                elif proc.returncode == 2 or "Vídeo de referência necessário" in stdout_text:
                    item["status"] = "⚠️ Requer Referência"
                    item["tag"] = "needs_ref"
                    needs_ref_count += 1
                else:
                    item["status"] = "❌ Falha"
                    item["tag"] = "failed"
                    fail_count += 1

            except Exception as e:
                item["status"] = "❌ Erro"
                item["tag"] = "failed"
                fail_count += 1

            self.after(0, self._update_tree_item, iid, item)
            self.after(0, lambda val=i+1: self.batch_pbar.config(value=val))

        self.after(0, self._on_batch_finish, success_count, needs_ref_count, fail_count, total)

    def _update_tree_item(self, iid, item):
        self.tree_batch.item(iid, values=(
            item["id"], item["filename"], format_bytes(item["size"]),
            item["status"], item["method"], item["frames"], item["res"], item["time"]
        ), tags=(item["tag"],))

    def _on_batch_finish(self, success, needs_ref, fail, total):
        self.batch_running = False
        self.btn_batch_start.config(state="normal")
        self.btn_batch_cancel.config(state="disabled")

        status_msg = f"Lote Finalizado: {success} recuperados com sucesso ({ (success/total*100):.1f}% ), {needs_ref} requerem referência, {fail} falhas."
        self.lbl_batch_status.config(text=status_msg, fg=PALETTE["success"] if success > 0 else PALETTE["text_muted"])

        msg = f"Processamento em massa concluído!\n\n" \
              f"• Total de vídeos: {total}\n" \
              f"• Recuperados com sucesso: {success}\n" \
              f"• Requerem vídeo de referência: {needs_ref}\n" \
              f"• Falhas / sem dados: {fail}\n\n" \
              f"Dica: Dê um duplo clique em qualquer item da tabela para reproduzir o vídeo recuperado!"
        messagebox.showinfo("Lote Concluído", msg)

    def _cancel_batch(self):
        if self.batch_running:
            self.batch_cancel_flag = True
            self.lbl_batch_status.config(text="Interrompendo lote após o vídeo atual...", fg=PALETTE["warning"])

    def _on_tree_double_click(self, event):
        selected = self.tree_batch.selection()
        if not selected:
            return
        iid = selected[0]
        for item in self.batch_queue:
            if str(item["id"]) == iid:
                out_p = item.get("out_path")
                if out_p and os.path.exists(out_p):
                    try:
                        if sys.platform == "win32":
                            os.startfile(out_p)
                        else:
                            subprocess.Popen(["xdg-open", out_p])
                    except Exception as e:
                        messagebox.showerror("Erro", f"Não foi possível abrir o player:\n{str(e)}")
                elif item["tag"] == "needs_ref":
                    self._set_ref_for_selected()
                break

    # --------------------------------------------------------------------------
    # MÉTODOS DE INSPEÇÃO, PLAYER E LOGS
    # --------------------------------------------------------------------------
    def _play_recovered(self):
        path = self.txt_out.get().strip()
        if not path or not os.path.exists(path):
            messagebox.showwarning("Aviso", "O vídeo restaurado não foi encontrado.")
            return

        try:
            if sys.platform == "win32":
                os.startfile(path)
            else:
                subprocess.Popen(["xdg-open", path])
        except Exception as e:
            messagebox.showerror("Erro", f"Não foi possível abrir o player:\n{str(e)}")

    def _inspect_video(self):
        path = self.txt_out.get().strip()
        if not path or not os.path.exists(path):
            messagebox.showwarning("Aviso", "Selecione ou gere um vídeo restaurado primeiro.")
            return

        self.txt_inspect.delete("1.0", tk.END)
        self.txt_inspect.insert(tk.END, f"Inspecionando arquivo: {path}\nExecutando verificação de integridade...\n\n")
        self.notebook.select(3)

        def worker():
            try:
                cmd = ["ffprobe", "-v", "error", "-show_streams", "-show_format", path]
                p = subprocess.run(cmd, capture_output=True, text=True,
                                   creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
                out = p.stdout if p.stdout else p.stderr

                cmd_dec = ["ffmpeg", "-v", "error", "-i", path, "-f", "null", "-"]
                p_dec = subprocess.run(cmd_dec, capture_output=True, text=True,
                                       creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)

                veredito = "✅ VEREDITO FORENSE: 100% dos quadros decodificados sem falhas ou corrupção de fluxo!\n" if p_dec.returncode == 0 else f"⚠️ Avisos de decodificação:\n{p_dec.stderr}\n"

                relat = f"{veredito}\n=== DADOS DETALHADOS DA TRILHA DE VÍDEO (FFPROBE) ===\n{out}"
                self.after(0, lambda: self.txt_inspect.insert(tk.END, relat))
            except Exception as e:
                self.after(0, lambda: self.txt_inspect.insert(tk.END, f"Erro: {str(e)}"))

        threading.Thread(target=worker, daemon=True).start()

    def _copy_log(self):
        t = self.txt_log.get("1.0", tk.END)
        self.clipboard_clear()
        self.clipboard_append(t)
        messagebox.showinfo("Copiado", "Log copiado para a área de transferência.")

    def _export_log(self):
        t = self.txt_log.get("1.0", tk.END)
        path = filedialog.asksaveasfilename(
            title="Salvar Relatório Forense",
            defaultextension=".txt",
            filetypes=[("Arquivo de Texto (*.txt)", "*.txt"), ("Todos os arquivos (*.*)", "*.*")]
        )
        if path:
            try:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(t)
                messagebox.showinfo("Sucesso", f"Relatório salvo com sucesso em:\n{path}")
            except Exception as e:
                messagebox.showerror("Erro", f"Falha ao salvar relatório: {str(e)}")

    def _clear_log(self):
        self.txt_log.delete("1.0", tk.END)

    def _open_github(self):
        import webbrowser
        webbrowser.open("https://github.com/pladix/FixCFTV")

def main():
    app = FixCFTVApp()
    app.mainloop()

if __name__ == "__main__":
    main()
