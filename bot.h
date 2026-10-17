/* 井字棋机器人 —— 纯计算、无 IO，ncurses 版和 Win32 版共用 */
#ifndef BOT_H
#define BOT_H

#include "game.h"

#define BOT_EASY 0   /* 能赢就赢，但从不堵 —— 玩家总能赢 */
#define BOT_HARD 1   /* minimax 搜到底，永不输，玩家最多平局 */

/* 播种全局随机数（rand）。机器人选点和界面的投色子都用它，
   所以只在程序启动时调一次。种子固定时行为可复现，方便测试。 */
void bot_init(unsigned seed);

/* 给当前该下的一方选一个空位，写进 *pr / *pc */
void bot_move(const Game *g, int level, int *pr, int *pc);

#endif
