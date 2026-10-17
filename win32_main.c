/* 井字棋 —— 原生 Windows 控制台实现（无任何第三方依赖）
 *
 * 为什么不用 PDCurses：它的 wincon 端口把虚拟屏按"一个字符一格"存储，
 * 而 Windows 控制台里汉字占 2 格，两者模型对不上，中文会被截断。
 *
 * 这里的做法是 SetConsoleCursorPosition + WriteConsoleW 逐行原地重绘：
 * 控制台自己会按字符的显示宽度前进，汉字占 2 列的问题由系统处理。
 */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include "game.h"
#include "store.h"
#include "bot.h"

#define GLYPH_WHITE "○"   /* 白子：空心 */
#define GLYPH_BLACK "●"   /* 黑子：实心 */

/* 棋盘每格 3 列宽，格线占 1 列/行 —— 3x3 的棋盘框共 13 列 x 7 行 */
#define BOARD_W 13
#define BOARD_H 7

/* Win32 颜色属性：高 4 位是背景，低 4 位是前景 */
#define ATTR_DEFAULT (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define ATTR_BOARD   (FOREGROUND_GREEN | FOREGROUND_BLUE)                       /* 青：棋盘线 */
#define ATTR_WHITE   (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | \
                      FOREGROUND_INTENSITY)                                     /* 亮白：白子 */
#define ATTR_BLACK   (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)      /* 黑字白底：黑子 */
#define ATTR_DIM     (FOREGROUND_INTENSITY)

/* 键值：用 curses 的编码习惯，方便和 main.c 对照 */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_RESIZE 0x104
#define KEY_MOUSE  0x105

static HANDLE hOut, hIn;
static DWORD  orig_in_mode;
static int    g_cols, g_rows;      /* 可见窗口尺寸 */
static int    g_ox, g_oy;          /* 窗口在缓冲区里的原点 */
static int    g_by, g_bx;          /* 棋盘框左上角 */
static int    g_mx, g_my;          /* 最近一次点击，已换算成窗口坐标 */
static bool   g_mouse_ok;          /* 控制台支不支持鼠标 */

/* ---------- 底层输出 ---------- */

/* 前景/背景对调，用于光标高亮 */
static WORD attr_reverse(WORD a) {
    return (WORD)(((a & 0x00F0) >> 4) | ((a & 0x000F) << 4));
}

/* 判断一个 UTF-16 码元在控制台里是否占两列（CJK 全角等） */
static int is_wide(wchar_t wc) {
    return (wc >= 0x1100 && wc <= 0x115F) ||
           (wc >= 0x2E80 && wc <= 0x303E) ||
           (wc >= 0x3041 && wc <= 0x33FF) ||
           (wc >= 0x3400 && wc <= 0x4DBF) ||
           (wc >= 0x4E00 && wc <= 0x9FFF) ||
           (wc >= 0xA000 && wc <= 0xA4CF) ||
           (wc >= 0xAC00 && wc <= 0xD7A3) ||
           (wc >= 0xF900 && wc <= 0xFAFF) ||
           (wc >= 0xFE30 && wc <= 0xFE6F) ||
           (wc >= 0xFF00 && wc <= 0xFF60) ||
           (wc >= 0xFFE0 && wc <= 0xFFE6);
}

static int utf8_to_wide(const char *utf8, wchar_t *out, int cap) {
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap);
}

/* 字符串在屏幕上占多少列 */
static int display_width(const char *utf8) {
    wchar_t wbuf[512];
    int n = utf8_to_wide(utf8, wbuf, 511);
    int w = 0;
    for (int i = 0; i < n - 1; i++)
        w += is_wide(wbuf[i]) ? 2 : 1;
    return w;
}

/* 把 UTF-8 字符串写到窗口坐标 (x, y) */
static void put(int x, int y, WORD attr, const char *utf8) {
    if (y < 0 || y >= g_rows || x < 0 || x >= g_cols) return;

    wchar_t wbuf[512];
    int n = utf8_to_wide(utf8, wbuf, 511);
    if (n <= 1) return;

    COORD c = { (SHORT)(g_ox + x), (SHORT)(g_oy + y) };
    SetConsoleCursorPosition(hOut, c);
    SetConsoleTextAttribute(hOut, attr);
    DWORD written;
    WriteConsoleW(hOut, wbuf, (DWORD)(n - 1), &written, NULL);
}

/* 用 s 的内容加空格补齐到 cols 列宽，避免上一帧的残留 */
static void put_padded(int x, int y, WORD attr, const char *s, int cols) {
    char buf[256];
    int n = snprintf(buf, sizeof buf, "%s", s);
    if (n < 0 || n >= (int)sizeof buf) return;
    int w = display_width(s);
    while (w < cols && n + 1 < (int)sizeof buf) {
        buf[n++] = ' ';
        w++;
    }
    buf[n] = 0;
    put(x, y, attr, buf);
}

static void clear_screen(void) {
    CONSOLE_SCREEN_BUFFER_INFO bi;
    GetConsoleScreenBufferInfo(hOut, &bi);
    DWORD total = (DWORD)bi.dwSize.X * (DWORD)bi.dwSize.Y;
    COORD home = { 0, 0 };
    DWORD n;
    FillConsoleOutputCharacterW(hOut, L' ', total, home, &n);
    FillConsoleOutputAttribute(hOut, ATTR_DEFAULT, total, home, &n);
}

/* ---------- 布局与绘制 ---------- */

static void layout(void) {
    CONSOLE_SCREEN_BUFFER_INFO bi;
    GetConsoleScreenBufferInfo(hOut, &bi);
    g_ox = bi.srWindow.Left;
    g_oy = bi.srWindow.Top;
    g_cols = bi.srWindow.Right - bi.srWindow.Left + 1;
    g_rows = bi.srWindow.Bottom - bi.srWindow.Top + 1;

    g_bx = (g_cols - BOARD_W) / 2;
    g_by = (g_rows - BOARD_H) / 2 - 1;
    if (g_bx < 1) g_bx = 1;
    if (g_by < 1) g_by = 1;
}

static int cell_y(int r) { return g_by + 1 + 2 * r; }
static int cell_x(int c) { return g_bx + 2 + 4 * c; }

/* 棋盘线内容不变，只在开始和尺寸变化时画一次 */
static void draw_board(void) {
    put(g_bx, g_by,     ATTR_BOARD, "┌───┬───┬───┐");
    put(g_bx, g_by + 1, ATTR_BOARD, "│   │   │   │");
    put(g_bx, g_by + 2, ATTR_BOARD, "├───┼───┼───┤");
    put(g_bx, g_by + 3, ATTR_BOARD, "│   │   │   │");
    put(g_bx, g_by + 4, ATTR_BOARD, "├───┼───┼───┤");
    put(g_bx, g_by + 5, ATTR_BOARD, "│   │   │   │");
    put(g_bx, g_by + 6, ATTR_BOARD, "└───┴───┴───┘");
}

/* 每帧刷新：棋子、光标高亮、状态行 */
static void draw_dynamic(const Game *g) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            bool here = (g->winner == EMPTY && r == g->cr && c == g->cc);
            int v = g->cell[r][c];

            WORD attr = ATTR_DEFAULT;
            const char *s = "   ";
            if (v == WHITE)     { attr = ATTR_WHITE; s = " " GLYPH_WHITE " "; }
            else if (v == BLACK){ attr = ATTR_BLACK; s = " " GLYPH_BLACK " "; }

            if (here) attr = attr_reverse(attr);   /* 光标格整体反显 */
            put(cell_x(c) - 1, cell_y(r), attr, s);
        }

    /* 状态行 */
    char text[128];
    WORD attr = ATTR_DEFAULT;
    if (g->winner == EMPTY) {
        attr = (g->cur == WHITE) ? ATTR_WHITE : ATTR_BLACK;
        snprintf(text, sizeof text, "%s 落子", (g->cur == WHITE) ? "白子 " GLYPH_WHITE
                                                                : "黑子 " GLYPH_BLACK);
    } else if (g->winner == DRAW) {
        snprintf(text, sizeof text, "平局！按 r 重开");
    } else {
        snprintf(text, sizeof text, "%s 胜！按 r 重开",
                 (g->winner == WHITE) ? "白子" : "黑子");
    }
    put_padded(g_bx, g_by + BOARD_H + 1, attr, text, 20);
}

static void draw_help(void) {
    put_padded(2, g_rows - 2, ATTR_DIM,
               "方向键移动    回车/空格落子    r 重开    q 返回菜单", 60);
}

/* ---------- 界面与鼠标 ---------- */

enum { SCR_MENU, SCR_LEVEL, SCR_DICE, SCR_GAME, SCR_RECORDS };

/* 点击落在第几项上：每项占两行（第 i 项在 y0+2i 和 y0+2i+1），宽 12 列。
   不在任何一项上返回 -1。 */
static int item_at(int mx, int my, int x0, int y0, int n) {
    if (mx < x0 || mx >= x0 + 12) return -1;
    for (int i = 0; i < n; i++) {
        int top = y0 + i * 2;
        if (my == top || my == top + 1) return i;
    }
    return -1;
}

/* 点击坐标反查棋盘格；没点在格子上返回 0。
   每格 3 列宽、格线占 1 列，行则只有格心那一行算数。 */
static int cell_at(int mx, int my, int *pr, int *pc) {
    int dr = my - (g_by + 1);
    int dc = mx - (g_bx + 1);
    if (dr < 0 || dr > 4 || dr % 2 != 0) return 0;
    if (dc < 0 || dc > 11 || dc % 4 > 2) return 0;
    *pr = dr / 2;
    *pc = dc / 4;
    return 1;
}

/* ---------- 菜单与记录 ---------- */

enum { MENU_BOT, MENU_PVP, MENU_CONTINUE, MENU_RECORDS, MENU_QUIT, MENU_N };

/* 每项固定 8 列宽（汉字占 2 列），高亮块才是等宽的 */
static const char *MENU_ITEMS[MENU_N] = {
    "人机对战", "双人对战", "继续    ", "查看记录", "退出    ",
};

/* 没有存档时「继续」不可选 */
static bool menu_enabled(const Store *st, int i) {
    return i != MENU_CONTINUE || st->has_save;
}

/* 菜单/难度页的原点 —— 绘制和鼠标命中判定共用，免得两边算得不一样 */
static int menu_x(void)  { int x = (g_cols - 12) / 2; return x < 0 ? 0 : x; }
static int menu_y(void)  { int y = g_rows / 2 - 6;    return y < 0 ? 0 : y; }
static int level_x(void) { int x = (g_cols - 12) / 2; return x < 0 ? 0 : x; }
static int level_y(void) { int y = g_rows / 2 - 3;    return y < 0 ? 0 : y; }

static void draw_menu(const Store *st, int sel) {
    int x = menu_x();
    int y = menu_y();

    put(x + 3, y - 2, ATTR_BOARD, "井字棋");

    for (int i = 0; i < MENU_N; i++) {
        bool on = menu_enabled(st, i);
        WORD attr = on ? ATTR_DEFAULT : ATTR_DIM;
        char buf[32];
        snprintf(buf, sizeof buf, "  %s  ", MENU_ITEMS[i]);
        if (i == sel) attr = attr_reverse(attr);
        put_padded(x, y + i * 2, attr, buf, 12);
    }

    put_padded(2, g_rows - 2, ATTR_DIM,
               g_mouse_ok ? "方向键选择    回车确认    鼠标可点"
                          : "方向键选择    回车确认", 40);

    /* 选中「继续」却没存档时明说一句，免得按回车没反应像是卡住了 */
    put_padded(x, y + MENU_N * 2, ATTR_DIM,
               (sel == MENU_CONTINUE && !st->has_save) ? "还没有存档" : "", 20);
}

/* ---------- 难度选择 ---------- */

enum { LEVEL_EASY, LEVEL_HARD, LEVEL_N };

static const char *LEVEL_ITEMS[LEVEL_N] = { "简单    ", "困难    " };
static const char *LEVEL_DESC[LEVEL_N]  = {
    "机器人会放水，你能赢",
    "机器人不会输，最多平局",
};

static void draw_level(int sel) {
    int x = level_x();
    int y = level_y();

    put(x + 3, y - 2, ATTR_BOARD, "选择难度");

    for (int i = 0; i < LEVEL_N; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "  %s  ", LEVEL_ITEMS[i]);
        WORD attr = (i == sel) ? attr_reverse(ATTR_DEFAULT) : ATTR_DEFAULT;
        put_padded(x, y + i * 2, attr, buf, 12);
    }

    put_padded(x, y + LEVEL_N * 2 + 1, ATTR_DIM, LEVEL_DESC[sel], 24);
    put_padded(2, g_rows - 2, ATTR_DIM, "方向键选择    回车确认    q 返回", 40);
}

/* ---------- 投色子 ---------- */

static void draw_dice(int you, int bot, bool done) {
    int x = g_cols / 2 - 13;
    int y = g_rows / 2 - 3;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    put(g_cols / 2 - 3, y - 2, ATTR_BOARD, "投色子");

    char buf[64];
    snprintf(buf, sizeof buf, "玩家 [ %d ]    机器人 [ %d ]", you, bot);
    put_padded(x, y, ATTR_DEFAULT, buf, 26);

    snprintf(buf, sizeof buf, "%s执白棋，先下", (you > bot) ? "玩家" : "机器人");
    put_padded(x, y + 2, ATTR_DEFAULT, done ? buf : "", 24);

    put_padded(2, g_rows - 2, ATTR_DIM, done ? "回车开始" : "回车投掷", 40);
}

static void draw_records(const Store *st) {
    int x = g_cols / 2 - 10;
    int y = g_rows / 2 - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    put(g_cols / 2 - 4, y - 2, ATTR_BOARD, "对局记录");

    char buf[64];
    put_padded(x, y, ATTR_DEFAULT, "人机对战", 20);
    snprintf(buf, sizeof buf, "玩家胜    %3d 局", st->bot_wins);
    put_padded(x + 2, y + 1, ATTR_DEFAULT, buf, 20);
    snprintf(buf, sizeof buf, "机器人胜  %3d 局", st->bot_losses);
    put_padded(x + 2, y + 2, ATTR_DEFAULT, buf, 20);
    snprintf(buf, sizeof buf, "平局      %3d 局", st->bot_draws);
    put_padded(x + 2, y + 3, ATTR_DEFAULT, buf, 20);

    put_padded(x, y + 5, ATTR_DEFAULT, "双人对战", 20);
    snprintf(buf, sizeof buf, "白子胜    %3d 局", st->white_wins);
    put_padded(x + 2, y + 6, ATTR_DEFAULT, buf, 20);
    snprintf(buf, sizeof buf, "黑子胜    %3d 局", st->black_wins);
    put_padded(x + 2, y + 7, ATTR_DEFAULT, buf, 20);
    snprintf(buf, sizeof buf, "平局      %3d 局", st->draws);
    put_padded(x + 2, y + 8, ATTR_DEFAULT, buf, 20);

    put_padded(2, g_rows - 2, ATTR_DIM, "按任意键返回", 40);
}

/* ---------- 对局状态 ---------- */

enum { MODE_PVP, MODE_BOT };

static int g_mode       = MODE_PVP;  /* 这一局是双人还是人机 */
static int g_level      = BOT_EASY;  /* 人机局的难度 */
static int g_bot_side   = 0;         /* 人机局里机器人执哪一方 */
static int g_human_side = 0;         /* 人机局里玩家执哪一方 */

/* 按当前的 mode / bot_side / level 开一局，并立刻落一份存档 ——
   免得开完局马上退出，下次「继续」又捡回上一局 */
static void start_game(Store *st, Game *g) {
    game_init(g);
    st->saved = *g;
    st->has_save = true;
    st->save_vs_bot = (g_mode == MODE_BOT);
    st->save_bot_side = g_bot_side;
    st->save_level = g_level;
    store_write(st);
}

/* 从存档把对局状态整个恢复回来 */
static void resume_game(Store *st, Game *g) {
    g_mode = st->save_vs_bot ? MODE_BOT : MODE_PVP;
    g_level = st->save_level;
    g_bot_side = st->save_bot_side;
    g_human_side = (g_bot_side == WHITE) ? BLACK : WHITE;
    *g = st->saved;
}

/* 刚下完一手：终局就记战绩并清存档，否则把进度存下来 */
static void after_place(Store *st, Game *g) {
    if (g->winner != EMPTY) {
        store_add_result(st, g->winner, g_mode == MODE_BOT, g_human_side);
        st->has_save = false;
    } else {
        st->saved = *g;
        st->has_save = true;
        st->save_vs_bot = (g_mode == MODE_BOT);
        st->save_bot_side = g_bot_side;
        st->save_level = g_level;
    }
    store_write(st);
}

static bool bot_turn(const Game *g) {
    return g_mode == MODE_BOT && g->winner == EMPTY && g->cur == g_bot_side;
}

/* ---------- 鼠标 ---------- */

/* 把一次点击翻译成等价的按键，之后走和键盘完全相同的那套处理；
   点空了、或这个界面不接受鼠标，就返回 0 当没发生 */
static int mouse_key(int scr, int mx, int my, int *sel, int *lsel, Game *g) {
    if (scr == SCR_MENU) {
        int i = item_at(mx, my, menu_x(), menu_y(), MENU_N);
        if (i < 0) return 0;
        *sel = i;
        return '\n';
    }
    if (scr == SCR_LEVEL) {
        int i = item_at(mx, my, level_x(), level_y(), LEVEL_N);
        if (i < 0) return 0;
        *lsel = i;
        return '\n';
    }
    if (scr == SCR_DICE)    return '\n';   /* 点哪都算回车：投掷 / 开始 */
    if (scr == SCR_RECORDS) return ' ';    /* 任意键返回 */

    if (scr == SCR_GAME) {
        int r, c;
        if (!cell_at(mx, my, &r, &c)) return 0;
        g->cr = r;          /* 光标跟过去，落子失败时也有反馈 */
        g->cc = c;
        return '\n';
    }
    return 0;
}

/* ---------- 输入 ---------- */

static int read_key(void) {
    INPUT_RECORD ir;
    DWORD n;
    for (;;) {
        if (!ReadConsoleInputW(hIn, &ir, 1, &n) || n == 0) return -1;

        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) return KEY_RESIZE;

        if (ir.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD *m = &ir.Event.MouseEvent;
            /* dwEventFlags 为 0 才是按下/抬起；按下时按钮位为 1，抬起时为 0。
               双击的第二次按下带 DOUBLE_CLICK 标志，会被这里跳过，
               但第一次按下已经处理过了，不影响手感。 */
            if (m->dwEventFlags != 0) continue;
            if (!(m->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) continue;
            g_mx = m->dwMousePosition.X - g_ox;   /* 缓冲区坐标 → 窗口坐标 */
            g_my = m->dwMousePosition.Y - g_oy;
            return KEY_MOUSE;
        }

        if (ir.EventType != KEY_EVENT) continue;
        if (!ir.Event.KeyEvent.bKeyDown) continue;

        switch (ir.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP:     return KEY_UP;
        case VK_DOWN:   return KEY_DOWN;
        case VK_LEFT:   return KEY_LEFT;
        case VK_RIGHT:  return KEY_RIGHT;
        case VK_RETURN: return '\n';
        case VK_SPACE:  return ' ';
        case VK_ESCAPE: return 27;
        default: break;
        }
        wchar_t ch = ir.Event.KeyEvent.uChar.UnicodeChar;
        if (ch >= 32 && ch < 127) return (int)ch;
    }
}

/* ---------- 主程序 ---------- */

static void ui_init(void) {
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hIn  = GetStdHandle(STD_INPUT_HANDLE);

    SetConsoleOutputCP(CP_UTF8);

    GetConsoleMode(hIn, &orig_in_mode);
    /* ENABLE_QUICK_EDIT_MODE 必须清掉：留着的话点击会被控制台当成选中文本吃掉，
       鼠标事件根本传不到这里。改这个位需要同时带上 ENABLE_EXTENDED_FLAGS。 */
    SetConsoleMode(hIn, ((orig_in_mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                                           ENABLE_QUICK_EDIT_MODE))
                         | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS
                         | ENABLE_MOUSE_INPUT));
    g_mouse_ok = true;   /* 控制台这边一直支持鼠标 */

    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &ci);
}

static void ui_cleanup(void) {
    clear_screen();
    SetConsoleCursorPosition(hOut, (COORD){ 0, 0 });
    SetConsoleMode(hIn, orig_in_mode);
    CONSOLE_CURSOR_INFO ci;
    GetConsoleCursorInfo(hOut, &ci);
    ci.bVisible = TRUE;
    SetConsoleCursorInfo(hOut, &ci);
}

int main(void) {
    ui_init();
    bot_init((unsigned)time(NULL));

    Store st;
    store_load(&st);

    int scr = SCR_MENU;
    int sel = MENU_BOT;
    int lsel = LEVEL_EASY;

    int dice_you = 0, dice_bot = 0;
    bool dice_done = false;

    Game g;
    game_init(&g);

    bool dirty = true;   /* 需要整屏重画（切屏或窗口尺寸变了） */
    bool running = true;
    while (running) {
        layout();
        bool too_small = (g_cols < BOARD_W + 2 || g_rows < BOARD_H + 6);

        /* 该机器人下就先让它下 —— 机器人执白时，开局这一步也在这里走 */
        if (scr == SCR_GAME && bot_turn(&g) && !too_small) {
            draw_dynamic(&g);      /* 先让玩家看清自己刚下的那手 */
            Sleep(400);

            int r, c;
            bot_move(&g, g_level, &r, &c);
            game_place(&g, r, c);
            after_place(&st, &g);
        }

        /* 棋盘线和帮助行内容固定，只在切屏 / 改尺寸时画一次 */
        if (dirty) {
            clear_screen();
            if (too_small) {
                put(0, 0, ATTR_DEFAULT, "终端太小，请放大窗口");
            } else if (scr == SCR_GAME) {
                draw_board();
                draw_help();
            }
            dirty = false;
        }

        /* put() 是原地覆盖，不闪；每轮重画一遍就够了 */
        if (!too_small) {
            if (scr == SCR_MENU)          draw_menu(&st, sel);
            else if (scr == SCR_LEVEL)    draw_level(lsel);
            else if (scr == SCR_DICE)     draw_dice(dice_you, dice_bot, dice_done);
            else if (scr == SCR_RECORDS)  draw_records(&st);
            else                          draw_dynamic(&g);
        }

        int k = read_key();

        /* 鼠标点击翻译成等价按键，往下走同一套处理 */
        if (k == KEY_MOUSE) {
            if (too_small) continue;
            k = mouse_key(scr, g_mx, g_my, &sel, &lsel, &g);
            if (k == 0) continue;
        }

        if (scr == SCR_MENU) {
            switch (k) {
            case KEY_UP:   sel = (sel + MENU_N - 1) % MENU_N; break;
            case KEY_DOWN: sel = (sel + 1) % MENU_N; break;
            case 'q': case 'Q': case 27:
                running = false;
                break;
            case '\n': case ' ':
                if (!menu_enabled(&st, sel)) break;
                if (sel == MENU_BOT) {
                    lsel = LEVEL_EASY;
                    scr = SCR_LEVEL; dirty = true;
                } else if (sel == MENU_PVP) {
                    g_mode = MODE_PVP;
                    start_game(&st, &g);
                    scr = SCR_GAME; dirty = true;
                } else if (sel == MENU_CONTINUE) {
                    resume_game(&st, &g);
                    scr = SCR_GAME; dirty = true;
                } else if (sel == MENU_RECORDS) {
                    scr = SCR_RECORDS; dirty = true;
                } else {
                    running = false;
                }
                break;
            default: break;
            }
        } else if (scr == SCR_LEVEL) {
            switch (k) {
            case KEY_UP:   lsel = (lsel + LEVEL_N - 1) % LEVEL_N; break;
            case KEY_DOWN: lsel = (lsel + 1) % LEVEL_N; break;
            case 'q': case 'Q': case 27:
                scr = SCR_MENU; dirty = true;
                break;
            case '\n': case ' ':
                g_level = (lsel == LEVEL_HARD) ? BOT_HARD : BOT_EASY;
                dice_you = dice_bot = 0;
                dice_done = false;
                scr = SCR_DICE; dirty = true;
                break;
            default: break;
            }
        } else if (scr == SCR_DICE) {
            if (k == 'q' || k == 'Q' || k == 27) {
                scr = SCR_MENU; dirty = true;
            } else if (k == '\n' || k == ' ') {
                if (!dice_done) {
                    for (int i = 0; i < 12; i++) {   /* 滚一会儿再定格 */
                        dice_you = rand() % 6 + 1;
                        dice_bot = rand() % 6 + 1;
                        draw_dice(dice_you, dice_bot, false);
                        Sleep(70);
                    }
                    do {                             /* 平手就重掷 */
                        dice_you = rand() % 6 + 1;
                        dice_bot = rand() % 6 + 1;
                    } while (dice_you == dice_bot);
                    dice_done = true;
                } else {
                    g_mode = MODE_BOT;
                    g_human_side = (dice_you > dice_bot) ? WHITE : BLACK;
                    g_bot_side = (g_human_side == WHITE) ? BLACK : WHITE;
                    start_game(&st, &g);
                    scr = SCR_GAME; dirty = true;
                }
            }
        } else if (scr == SCR_RECORDS) {
            if (k != -1) { scr = SCR_MENU; dirty = true; }   /* 任意键返回 */
        } else {
            switch (k) {
            case 'q': case 'Q': case 27:
                scr = SCR_MENU; dirty = true;
                break;
            case 'r': case 'R':
                start_game(&st, &g);   /* 重开一局，机器人执子不变 */
                break;
            case KEY_UP:    if (g.cr > 0) g.cr--; break;
            case KEY_DOWN:  if (g.cr < 2) g.cr++; break;
            case KEY_LEFT:  if (g.cc > 0) g.cc--; break;
            case KEY_RIGHT: if (g.cc < 2) g.cc++; break;
            case '\n': case ' ':
                if (bot_turn(&g)) break;   /* 人机局里轮不到玩家就别落子 */
                game_place(&g, g.cr, g.cc);
                after_place(&st, &g);
                break;
            case KEY_RESIZE:
                dirty = true;
                break;
            default: break;
            }
        }
    }

    ui_cleanup();
    return 0;
}
