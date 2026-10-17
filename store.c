/* 存档与战绩的读写 —— 与界面无关，ncurses 版和 Win32 版共用
 *
 * 存在 exe 同目录的 tictactoe.dat 里，格式是纯 ASCII 的「键 值」行：
 *
 *     white_wins 3
 *     black_wins 2
 *     draws 1
 *     bot_wins 1
 *     bot_losses 4
 *     bot_draws 2
 *     save 1
 *     cur 1
 *     cells 0 0 0 0 1 0 0 0 2
 *     save_vs_bot 1
 *     save_bot_side 2
 *     save_level 1
 *
 * 故意只用 ASCII：省得两个后端还要操心文件本身的编码。
 * 认不出的键直接跳过，缺键就用默认值，所以文件损坏也不会崩；
 * 也因此加新键不会让旧文件失效（旧战绩照读，新项从零开始）。
 */
#include "store.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#  include <wchar.h>
#else
#  include <unistd.h>
#endif

#define STORE_FILE "tictactoe.dat"

/* 打开 exe 同目录下的存档文件，失败返回 NULL（调用方当作空数据）
 *
 * Windows 侧走宽字符路径：exe 目录里可能有中文，窄字符的 fopen 会按 ANSI
 * 代码页解释而打不开。Cygwin 侧 /proc/self/exe 直接给出真实路径。 */
static FILE *store_open(int writing) {
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return NULL;
    while (n > 0 && path[n - 1] != L'\\' && path[n - 1] != L'/') n--;  /* 砍掉文件名 */
    if (n + sizeof STORE_FILE > MAX_PATH) return NULL;
    wcscpy(path + n, L"tictactoe.dat");
    return _wfopen(path, writing ? L"w" : L"r");
#else
    char path[4096];
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) return NULL;
    path[n] = 0;
    char *slash = strrchr(path, '/');
    if (!slash) return NULL;
    slash[1] = 0;
    if (strlen(path) + sizeof STORE_FILE > sizeof path) return NULL;
    strcat(path, STORE_FILE);
    return fopen(path, writing ? "w" : "r");
#endif
}

void store_load(Store *s) {
    memset(s, 0, sizeof *s);

    FILE *f = store_open(0);
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[32];
        if (sscanf(line, "%31s", key) != 1) continue;

        if      (strcmp(key, "white_wins") == 0) sscanf(line, "%*s %d", &s->white_wins);
        else if (strcmp(key, "black_wins") == 0) sscanf(line, "%*s %d", &s->black_wins);
        else if (strcmp(key, "draws") == 0)      sscanf(line, "%*s %d", &s->draws);
        else if (strcmp(key, "bot_wins") == 0)   sscanf(line, "%*s %d", &s->bot_wins);
        else if (strcmp(key, "bot_losses") == 0) sscanf(line, "%*s %d", &s->bot_losses);
        else if (strcmp(key, "bot_draws") == 0)  sscanf(line, "%*s %d", &s->bot_draws);
        else if (strcmp(key, "save") == 0) {
            int v = 0;
            sscanf(line, "%*s %d", &v);
            s->has_save = (v != 0);
        }
        else if (strcmp(key, "save_vs_bot") == 0) {
            int v = 0;
            sscanf(line, "%*s %d", &v);
            s->save_vs_bot = (v != 0);
        }
        else if (strcmp(key, "save_bot_side") == 0) sscanf(line, "%*s %d", &s->save_bot_side);
        else if (strcmp(key, "save_level") == 0)    sscanf(line, "%*s %d", &s->save_level);
        else if (strcmp(key, "cur") == 0) sscanf(line, "%*s %d", &s->saved.cur);
        else if (strcmp(key, "cells") == 0) {
            int v[9];
            if (sscanf(line, "%*s %d %d %d %d %d %d %d %d %d",
                       &v[0], &v[1], &v[2], &v[3], &v[4],
                       &v[5], &v[6], &v[7], &v[8]) == 9)
                for (int i = 0; i < 9; i++)
                    s->saved.cell[i / 3][i % 3] = v[i];
        }
    }
    fclose(f);

    /* 校验存档：任何一处不对就当没有存档，让用户重开一局总比载入个坏局面强 */
    if (!s->has_save) return;

    s->saved.moves = 0;
    for (int r = 0; r < 3 && s->has_save; r++)
        for (int c = 0; c < 3; c++) {
            int v = s->saved.cell[r][c];
            if (v != EMPTY && v != WHITE && v != BLACK) { s->has_save = false; break; }
            if (v != EMPTY) s->saved.moves++;
        }
    if (s->has_save && s->saved.cur != WHITE && s->saved.cur != BLACK) s->has_save = false;
    if (s->has_save && game_winner(&s->saved) != EMPTY)                s->has_save = false;
    if (s->has_save && s->save_vs_bot &&
        s->save_bot_side != WHITE && s->save_bot_side != BLACK)        s->has_save = false;
    if (!s->has_save) return;

    s->saved.winner = EMPTY;
    s->saved.cr = s->saved.cc = 1;   /* 光标位置不值得存，回到中心 */
}

void store_write(const Store *s) {
    FILE *f = store_open(1);
    if (!f) return;

    fprintf(f, "white_wins %d\n", s->white_wins);
    fprintf(f, "black_wins %d\n", s->black_wins);
    fprintf(f, "draws %d\n", s->draws);
    fprintf(f, "bot_wins %d\n", s->bot_wins);
    fprintf(f, "bot_losses %d\n", s->bot_losses);
    fprintf(f, "bot_draws %d\n", s->bot_draws);
    fprintf(f, "save %d\n", s->has_save ? 1 : 0);
    if (s->has_save) {
        fprintf(f, "cur %d\n", s->saved.cur);
        fprintf(f, "cells");
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                fprintf(f, " %d", s->saved.cell[r][c]);
        fprintf(f, "\n");
        fprintf(f, "save_vs_bot %d\n", s->save_vs_bot ? 1 : 0);
        fprintf(f, "save_bot_side %d\n", s->save_bot_side);
        fprintf(f, "save_level %d\n", s->save_level);
    }
    fclose(f);
}

void store_add_result(Store *s, int winner, bool vs_bot, int human) {
    if (!vs_bot) {
        if      (winner == WHITE) s->white_wins++;
        else if (winner == BLACK) s->black_wins++;
        else if (winner == DRAW)  s->draws++;
        return;
    }
    if      (winner == DRAW)   s->bot_draws++;
    else if (winner == human)  s->bot_wins++;    /* 玩家执的一方赢了 */
    else                       s->bot_losses++;  /* 机器人赢了 */
}
