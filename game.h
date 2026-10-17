/* 井字棋的游戏逻辑 —— 与界面无关，ncurses 版和 Win32 版共用 */
#ifndef GAME_H
#define GAME_H

#define EMPTY 0
#define WHITE 1
#define BLACK 2
#define DRAW  3

typedef struct {
    int cell[3][3];   /* 0 空位 / 1 白子 / 2 黑子 */
    int cur;          /* 当前该谁下 */
    int cr, cc;       /* 光标所在格 */
    int winner;       /* EMPTY 未分胜负 / WHITE / BLACK / DRAW */
    int moves;        /* 已落子数 */
} Game;

void game_init(Game *g);
int  game_winner(const Game *g);
void game_place(Game *g, int r, int c);

#endif
