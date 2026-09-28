CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=c99
INCLUDES = -Iinclude
LDFLAGS = 

FFMPEG_DIR ?= C:/Users/luking/tools/ffmpeg-9.0.2-full_build-shared
ifneq ($(wildcard $(FFMPEG_DIR)/include),)
    INCLUDES += -I$(FFMPEG_DIR)/include
    LDFLAGS += -L$(FFMPEG_DIR)/lib
endif

LIBS = $(LDFLAGS) -lavformat -lavcodec -lavutil -lswresample -lm
GUI_LIBS = -mwindows -lcomctl32 -lcomdlg32 -lgdi32 -luser32

SRC_CLI = src/main.c src/healer.c src/nal_parser.c src/utils.c
OBJ_CLI = $(SRC_CLI:.c=.o)
TARGET_CLI = FixCFTV

SRC_GUI = src/gui_main.c src/healer.c src/nal_parser.c src/utils.c
OBJ_GUI = src/gui_main.o src/healer.o src/nal_parser.o src/utils.o
TARGET_GUI = FixCFTV_GUI.exe

ifeq ($(OS),Windows_NT)
    BIN_CLI = $(TARGET_CLI).exe
else
    BIN_CLI = $(TARGET_CLI)
endif

all: $(BIN_CLI) $(TARGET_GUI)

cli: $(BIN_CLI)

gui: $(TARGET_GUI)

$(BIN_CLI): $(OBJ_CLI)
	$(CC) $(CFLAGS) $(OBJ_CLI) -o $@ $(LIBS)
	@echo "[+] FixCFTV CLI compilado com sucesso: $(BIN_CLI)"

$(TARGET_GUI): $(OBJ_GUI)
	$(CC) $(CFLAGS) $(OBJ_GUI) -o $@ $(LIBS) $(GUI_LIBS)
	@echo "[+] FixCFTV GUI Win32 compilado com sucesso: $(TARGET_GUI)"

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -f src/*.o $(BIN_CLI) $(TARGET_CLI) $(TARGET_GUI) 2>/dev/null || del /f /q src\*.o $(BIN_CLI) $(TARGET_CLI) $(TARGET_GUI) 2>nul || true
	@echo "[+] Limpeza concluída com sucesso."

test: $(BIN_CLI)
	@pwsh -ExecutionPolicy Bypass -File tests/run_tests.ps1 || sh tests/run_tests.sh

.PHONY: all cli gui clean test
