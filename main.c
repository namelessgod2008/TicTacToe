/* 井字棋 —— ncurses TUI 实现
 *
 * 棋盘逻辑用 int[3][3]：0 空位 / 1 白子 / 2 黑子
 * 操作：方向键移动光标，回车或空格落子，r 重开，q 退出
 */
#include <curses.h>   /* ncurses 和 PDCurses 都提供这个头，两边都能编 */
#include <locale.h>
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

enum { CP_BOARD = 1, CP_WHITE, CP_BLACK };

/* ---------- 布局与绘制 ---------- */

static int g_by, g_bx;   /* 棋盘框左上角 */

static void layout(void) {
    g_bx = (COLS - BOARD_W) / 2;
    g_by = (LINES - BOARD_H) / 2 - 1;
    if (g_bx < 1) g_bx = 1;
    if (g_by < 1) g_by = 1;
}

/* 第 r 行第 c 列格子的中心坐标 */
static int cell_y(int r) { return g_by + 1 + 2 * r; }
static int cell_x(int c) { return g_bx + 2 + 4 * c; }

static void draw_board(void) {
    attron(COLOR_PAIR(CP_BOARD));
    mvaddstr(g_by,     g_bx, "┌───┬───┬───┐");
    mvaddstr(g_by + 1, g_bx, "│   │   │   │");
    mvaddstr(g_by + 2, g_bx, "├───┼───┼───┤");
    mvaddstr(g_by + 3, g_bx, "│   │   │   │");
    mvaddstr(g_by + 4, g_bx, "├───┼───┼───┤");
    mvaddstr(g_by + 5, g_bx, "│   │   │   │");
    mvaddstr(g_by + 6, g_bx, "└───┴───┴───┘");
    attroff(COLOR_PAIR(CP_BOARD));
}

/* 逐格画子；光标所在格连同内腔一起反显 */
static void draw_cells(const Game *g) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            bool here = (g->winner == EMPTY && r == g->cr && c == g->cc);
            int y = cell_y(r), x = cell_x(c);
            int v = g->cell[r][c];

            if (here) attron(A_REVERSE);
            mvaddch(y, x - 1, ' ');
            mvaddch(y, x,     ' ');
            mvaddch(y, x + 1, ' ');

            if (v != EMPTY) {
                int pair = (v == WHITE) ? CP_WHITE : CP_BLACK;
                /* 白子始终加粗：多数终端里 COLOR_WHITE 是浅灰，加粗才是纯白 */
                bool bold = (v == WHITE) || (g->winner == v);
                attron(COLOR_PAIR(pair));
                if (bold) attron(A_BOLD);
                mvaddstr(y, x, (v == WHITE) ? GLYPH_WHITE : GLYPH_BLACK);
                if (bold) attroff(A_BOLD);
                attroff(COLOR_PAIR(pair));
            }
            if (here) attroff(A_REVERSE);
        }
}

static void draw_status(const Game *g) {
    int y = g_by + BOARD_H + 1;
    move(y, g_bx);
    clrtoeol();

    if (g->winner == EMPTY) {
        int pair = (g->cur == WHITE) ? CP_WHITE : CP_BLACK;
        attron(COLOR_PAIR(pair));
        printw("%s 落子", (g->cur == WHITE) ? "白子 ○" : "黑子 ●");
        attroff(COLOR_PAIR(pair));
    } else if (g->winner == DRAW) {
        printw("平局！按 r 重开");
    } else {
        printw("%s 胜！按 r 重开", (g->winner == WHITE) ? "白子" : "黑子");
    }
}

static void draw_help(void) {
    attron(A_DIM);
    mvaddstr(LINES - 2, 2, "方向键移动    回车/空格落子    r 重开    q 返回菜单");
    attroff(A_DIM);
}

/* 一整屏棋局 */
static void draw_game(const Game *g) {
    layout();
    draw_board();
    draw_cells(g);
    draw_status(g);
    draw_help();
}

/* ---------- 界面与鼠标 ---------- */

enum { SCR_MENU, SCR_LEVEL, SCR_DICE, SCR_GAME, SCR_RECORDS };

static bool g_mouse_ok;   /* 终端支不支持鼠标，取自 mousemask() */

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
static int menu_x(void)  { int x = COLS / 2 - 6; return x < 0 ? 0 : x; }
static int menu_y(void)  { int y = LINES / 2 - 6; return y < 0 ? 0 : y; }
static int level_x(void) { int x = COLS / 2 - 6; return x < 0 ? 0 : x; }
static int level_y(void) { int y = LINES / 2 - 3; return y < 0 ? 0 : y; }

static void draw_menu(const Store *st, int sel) {
    int x = menu_x();
    int y = menu_y();

    attron(A_BOLD);
    mvaddstr(y - 2, x + 3, "井字棋");
    attroff(A_BOLD);

    for (int i = 0; i < MENU_N; i++) {
        bool on = menu_enabled(st, i);
        if (i == sel) attron(A_REVERSE);
        if (!on)      attron(A_DIM);
        mvprintw(y + i * 2, x, "  %s  ", MENU_ITEMS[i]);
        if (!on)      attroff(A_DIM);
        if (i == sel) attroff(A_REVERSE);
    }

    attron(A_DIM);
    mvaddstr(LINES - 2, 2, g_mouse_ok ? "方向键选择    回车确认    鼠标可点"
                                      : "方向键选择    回车确认");
    attroff(A_DIM);

    /* 选中「继续」却没存档时明说一句，免得按回车没反应像是卡住了 */
    move(y + MENU_N * 2, x);
    clrtoeol();
    if (sel == MENU_CONTINUE && !st->has_save) printw("还没有存档");
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

    attron(A_BOLD);
    mvaddstr(y - 2, x + 3, "选择难度");
    attroff(A_BOLD);

    for (int i = 0; i < LEVEL_N; i++) {
        if (i == sel) attron(A_REVERSE);
        mvprintw(y + i * 2, x, "  %s  ", LEVEL_ITEMS[i]);
        if (i == sel) attroff(A_REVERSE);
    }

    attron(A_DIM);
    mvaddstr(y + LEVEL_N * 2 + 1, x, LEVEL_DESC[sel]);
    mvaddstr(LINES - 2, 2, "方向键选择    回车确认    q 返回");
    attroff(A_DIM);
}

/* ---------- 投色子 ---------- */

static void draw_dice(int you, int bot, bool done) {
    int x = COLS / 2 - 13;
    int y = LINES / 2 - 3;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    attron(A_BOLD);
    mvaddstr(y - 2, COLS / 2 - 3, "投色子");
    attroff(A_BOLD);

    mvprintw(y, x, "玩家 [ %d ]    机器人 [ %d ]", you, bot);

    move(y + 2, x);
    clrtoeol();
    if (done) printw("%s执白棋，先下", (you > bot) ? "玩家" : "机器人");

    attron(A_DIM);
    mvaddstr(LINES - 2, 2, done ? "回车开始" : "回车投掷");
    attroff(A_DIM);
}

static void draw_records(const Store *st) {
    int x = COLS / 2 - 10;
    int y = LINES / 2 - 4;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    attron(A_BOLD);
    mvaddstr(y - 2, COLS / 2 - 4, "对局记录");
    attroff(A_BOLD);

    mvaddstr(y, x, "人机对战");
    mvprintw(y + 1, x + 2, "玩家胜    %3d 局", st->bot_wins);
    mvprintw(y + 2, x + 2, "机器人胜  %3d 局", st->bot_losses);
    mvprintw(y + 3, x + 2, "平局      %3d 局", st->bot_draws);

    mvaddstr(y + 5, x, "双人对战");
    mvprintw(y + 6, x + 2, "白子胜    %3d 局", st->white_wins);
    mvprintw(y + 7, x + 2, "黑子胜    %3d 局", st->black_wins);
    mvprintw(y + 8, x + 2, "平局      %3d 局", st->draws);

    attron(A_DIM);
    mvaddstr(LINES - 2, 2, "按任意键返回");
    attroff(A_DIM);
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

/* ---------- 主循环 ---------- */

int main(void) {
    setlocale(LC_ALL, "");   /* 必须：否则制表符、●○ 和中文都画不出来 */
    bot_init((unsigned)time(NULL));

    initscr();
    /* 两个位都要开：只开 CLICKED 的话，一旦 ncurses 把按下和松开读成两批，
       它会超时改报 PRESSED，而该位不在掩码里 —— 事件就整个丢了（实测会偶发）。
       两个位都开时，按下+松开报一个 CLICKED，只有按下则报一个 PRESSED，都只触发一次。 */
    g_mouse_ok = (mousemask(BUTTON1_CLICKED | BUTTON1_PRESSED, NULL) &
                  (BUTTON1_CLICKED | BUTTON1_PRESSED)) != 0;
    cbreak();
    noecho();
    keypad(stdscr, TRUE);    /* 必须：否则读不到方向键 */
    curs_set(0);

    if (!has_colors()) {
        endwin();
        fprintf(stderr, "该终端不支持颜色\n");
        return 1;
    }
    start_color();
    use_default_colors();
    init_pair(CP_BOARD, COLOR_CYAN,    -1);
    init_pair(CP_WHITE, COLOR_WHITE,   -1);            /* 白子：白字，底色跟随终端 */
    init_pair(CP_BLACK, COLOR_BLACK,   COLOR_WHITE);   /* 黑子：黑字配白底 —— 纯黑字在深色/半透明背景上会看不见 */

    Store st;
    store_load(&st);

    int scr = SCR_MENU;
    int sel = MENU_BOT;
    int lsel = LEVEL_EASY;

    int dice_you = 0, dice_bot = 0;
    bool dice_done = false;

    Game g;
    game_init(&g);

    bool running = true;
    while (running) {
        /* 该机器人下就先让它下 —— 机器人执白时，开局这一步也在这里走 */
        if (scr == SCR_GAME && bot_turn(&g)) {
            erase();
            draw_game(&g);          /* 先让玩家看清自己刚下的那手 */
            refresh();
            napms(400);

            int r, c;
            bot_move(&g, g_level, &r, &c);
            game_place(&g, r, c);
            after_place(&st, &g);
        }

        erase();
        if (COLS < BOARD_W + 2 || LINES < BOARD_H + 6) {
            mvaddstr(0, 0, "终端太小，请放大窗口");
        } else if (scr == SCR_MENU) {
            draw_menu(&st, sel);
        } else if (scr == SCR_LEVEL) {
            draw_level(lsel);
        } else if (scr == SCR_DICE) {
            draw_dice(dice_you, dice_bot, dice_done);
        } else if (scr == SCR_RECORDS) {
            draw_records(&st);
        } else {
            draw_game(&g);
        }
        refresh();

        int ch = getch();

        /* 鼠标点击翻译成等价按键，往下走同一套处理 */
        if (ch == KEY_MOUSE) {
            MEVENT ev;
            bool too_small = (COLS < BOARD_W + 2 || LINES < BOARD_H + 6);
            if (getmouse(&ev) != OK || too_small) continue;
            ch = mouse_key(scr, ev.x, ev.y, &sel, &lsel, &g);
            if (ch == 0) continue;
        }

        if (scr == SCR_MENU) {
            switch (ch) {
            case KEY_UP:   sel = (sel + MENU_N - 1) % MENU_N; break;
            case KEY_DOWN: sel = (sel + 1) % MENU_N; break;
            case 'q': case 'Q': case 27:
                running = false;
                break;
            case '\n': case ' ':
                if (!menu_enabled(&st, sel)) break;
                if (sel == MENU_BOT) {
                    lsel = LEVEL_EASY;
                    scr = SCR_LEVEL;
                } else if (sel == MENU_PVP) {
                    g_mode = MODE_PVP;
                    start_game(&st, &g);
                    scr = SCR_GAME;
                } else if (sel == MENU_CONTINUE) {
                    resume_game(&st, &g);
                    scr = SCR_GAME;
                } else if (sel == MENU_RECORDS) {
                    scr = SCR_RECORDS;
                } else {
                    running = false;
                }
                break;
            }
        } else if (scr == SCR_LEVEL) {
            switch (ch) {
            case KEY_UP:   lsel = (lsel + LEVEL_N - 1) % LEVEL_N; break;
            case KEY_DOWN: lsel = (lsel + 1) % LEVEL_N; break;
            case 'q': case 'Q': case 27:
                scr = SCR_MENU;
                break;
            case '\n': case ' ':
                g_level = (lsel == LEVEL_HARD) ? BOT_HARD : BOT_EASY;
                dice_you = dice_bot = 0;
                dice_done = false;
                scr = SCR_DICE;
                break;
            }
        } else if (scr == SCR_DICE) {
            if (ch == 'q' || ch == 'Q' || ch == 27) {
                scr = SCR_MENU;
            } else if (ch == '\n' || ch == ' ') {
                if (!dice_done) {
                    for (int i = 0; i < 12; i++) {   /* 滚一会儿再定格 */
                        dice_you = rand() % 6 + 1;
                        dice_bot = rand() % 6 + 1;
                        erase();
                        draw_dice(dice_you, dice_bot, false);
                        refresh();
                        napms(70);
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
                    scr = SCR_GAME;
                }
            }
        } else if (scr == SCR_RECORDS) {
            scr = SCR_MENU;   /* 任意键返回 */
        } else {
            switch (ch) {
            case 'q': case 'Q': case 27:
                scr = SCR_MENU;
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
                break;   /* 下一轮 erase + layout 会自动重排 */
            }
        }
    }

    endwin();
    return 0;
}
