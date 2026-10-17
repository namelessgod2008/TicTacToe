/* 存档与战绩的读写 —— 与界面无关，ncurses 版和 Win32 版共用 */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include "game.h"

typedef struct {
    /* 双人对战战绩 */
    int white_wins;  /* 白子胜局数 */
    int black_wins;  /* 黑子胜局数 */
    int draws;       /* 平局数 */

    /* 人机对战战绩，都是从玩家的角度记的 */
    int bot_wins;    /* 玩家胜 */
    int bot_losses;  /* 机器人胜 */
    int bot_draws;   /* 平局 */

    bool has_save;      /* 是否有未下完的存档 */
    Game saved;         /* 存档局面，has_save 为真时有效 */
    bool save_vs_bot;   /* 存档是人机局（否则是双人局） */
    int  save_bot_side; /* 人机局里机器人执 WHITE / BLACK */
    int  save_level;    /* 难度，取值同 bot.h 的 BOT_EASY / BOT_HARD */
} Store;

/* 从文件读入；文件不存在或内容不合法时退回全零 / 无存档 */
void store_load(Store *s);

/* 整份写回文件；写不进去就静默放弃（比如 exe 放在只读目录里） */
void store_write(const Store *s);

/* 把一局的结果计入战绩。winner 取 WHITE / BLACK / DRAW；
   vs_bot 为真时记到人机战绩，human 是玩家执的一方 */
void store_add_result(Store *s, int winner, bool vs_bot, int human);

#endif
