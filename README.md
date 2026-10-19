# TicTacToe 井字棋

一个用 C（C23）写的终端井字棋。**同一套游戏逻辑配两套界面后端**：Cygwin / Unix 走 ncurses，原生 Windows 走 Win32 控制台 API。

原生那个后端零第三方依赖，产出**单个 exe**，只用到 `KERNEL32.dll` 和 `msvcrt.dll`（Windows 自带），可以直接发给别人运行。

```
            井字棋

        ▶ 人机对战
          双人对战
          继续
          查看记录
          退出

    方向键选择    回车确认    鼠标可点
```

```
    ┌───┬───┬───┐
    │ ○ │   │ ● │
    ├───┼───┼───┤
    │   │ ○ │   │
    ├───┼───┼───┤
    │ ● │   │   │
    └───┴───┴───┘
    白子 ○ 落子

  方向键移动  回车/空格落子  r 重开  q 返回菜单
```

## 特性

- **双人对战** —— 同一台机器上两人轮流下
- **人机对战** —— 两档难度：
  - **简单**：能赢就赢，但从不堵你，所以你一定赢得下来
  - **困难**：minimax 搜到底，**永不输**，你最多下成平局
- **投色子开局** —— 人机对战时双方各掷 1~6，点数大的执白先下，平手重掷，带滚动动画
- **存档与继续** —— 下到一半退出，下次打开接着下
- **对局记录** —— 人机、双人分开统计胜负平
- **鼠标操作** —— 点击即生效（详见下面的平台注意事项）
- **中文界面** —— 棋盘、状态、菜单全中文，制表符画的棋盘

## 构建

需要 **CMake ≥ 4.2**。

| 平台 | 后端 | 源文件 | 依赖 |
|---|---|---|---|
| Cygwin / Unix | ncurses | `main.c` + `game.c` + `bot.c` + `store.c` | ncurses 宽字符版 |
| 原生 Windows | Win32 控制台 API | `win32_main.c` + `game.c` + `bot.c` + `store.c` | 无 |

`CMakeLists.txt` 按平台自动选后端，不用手工指定。

```bash
cmake -S . -B build
cmake --build build
```

可执行文件直接产出到**项目根目录**。

> **目录名请用纯 ASCII。** 路径里出现非 ASCII 字符时，MinGW 的 `ld.exe` 按系统 ANSI 代码页编码文件名会失败，导致链接不出任何 exe（Cygwin 的 ld 没这个问题）。

## 操作

| 界面 | 键盘 | 鼠标 |
|---|---|---|
| 主菜单 | ↑↓ 选择，回车确认 | 点哪项就执行哪项 |
| 难度选择 | ↑↓ 选择，回车确认 | 同上 |
| 投色子 | 回车投掷 → 回车开始 | 点任意处，等同回车 |
| 棋局 | 方向键移动，回车/空格落子，`r` 重开，`q` 返回菜单 | 点格子落子 |
| 对局记录 | 任意键返回 | 点任意处返回 |

「继续」在**没有存档时会变暗且不可选**。终端太小（小于 15 列 × 13 行）时提示「终端太小，请放大窗口」。

## 存档

存档和战绩写在 **exe 同目录**的 `tictactoe.dat` 里，纯 ASCII 的「键 值」行格式：

```
white_wins 3
black_wins 2
draws 1
bot_wins 1
bot_losses 4
bot_draws 2
save 1
cur 1
cells 0 0 0 0 1 0 0 0 2
save_vs_bot 1
save_bot_side 2
save_level 1
```

认不出的键会跳过、缺键用默认值，所以**文件损坏不会崩**，加新字段也不会让旧存档失效。每落一子写一次；一局终局时把结果计入战绩并清掉存档。

想清空记录，直接删掉这个文件即可。

## 项目结构

```
game.h / game.c   游戏规则：棋盘、判胜、判平、换手
bot.h / bot.c     机器人选点（简单启发式 + minimax）
store.h / store.c 存档与战绩的文件读写
main.c            ncurses 后端
win32_main.c      Win32 控制台后端
```

前三个是纯逻辑，与界面无关，两个后端共用；后两个各自只被一个平台编译。两个后端保持**镜像结构**（`layout` / `draw_board` / `draw_cells` / `draw_status` / `draw_help` 一一对应）。

## 平台注意事项

- **鼠标**：Win32 后端在 cmd.exe 和 Windows Terminal 里都能用。**ncurses 后端只有 mintty 里能用** —— Cygwin 程序跑在 cmd.exe / Windows Terminal 里时会用 `cygwin` 这个 terminfo 条目，它没有 `kmous` 能力。程序会据此决定要不要在提示行显示「鼠标可点」，所以看不到提示就是当前终端不支持。
- **终端编码**：ncurses 版要求终端支持 UTF-8 和颜色，否则会退出并提示「该终端不支持颜色」。
- **Windows 控制台**：Win32 版会关掉「快速编辑模式」，否则鼠标点击会被控制台当成选中文本吃掉。代价是这个窗口里不能再用鼠标选文字复制。

---

# TicTacToe

A terminal tic-tac-toe game written in C (C23). **One set of game logic, two UI backends**: ncurses for Cygwin / Unix, and the Win32 console API for native Windows.

The native backend has zero third-party dependencies and produces a **single .exe** that only uses `KERNEL32.dll` and `msvcrt.dll` (both ship with Windows) — you can just send it to someone.

```
            井字棋

        ▶ 人机对战
          双人对战
          继续
          查看记录
          退出

    方向键选择    回车确认    鼠标可点
```

```
    ┌───┬───┬───┐
    │ ○ │   │ ● │
    ├───┼───┼───┤
    │   │ ○ │   │
    ├───┼───┼───┤
    │ ● │   │   │
    └───┴───┴───┘
    白子 ○ 落子

  方向键移动  回车/空格落子  r 重开  q 返回菜单
```

## Features

- **Two-player** — two people taking turns on the same machine
- **Versus the computer** — two difficulty levels:
  - **Easy**: takes a win when it sees one, but never blocks, so you always can win
  - **Hard**: full minimax, **never loses** — the best you can get is a draw
- **Dice roll to start** — each side rolls 1–6, higher roll plays white (and moves first); ties are re-rolled, with a rolling animation
- **Save & continue** — quit mid-game and pick it up next time
- **Match records** — win/loss/draw tallied separately for bot games and two-player games
- **Mouse support** — click to act (see the platform notes below)
- **Chinese UI** — board, status line and menus are all in Chinese

## Building

Requires **CMake ≥ 4.2**.

| Platform | Backend | Sources | Dependencies |
|---|---|---|---|
| Cygwin / Unix | ncurses | `main.c` + `game.c` + `bot.c` + `store.c` | wide-character ncurses |
| Native Windows | Win32 console API | `win32_main.c` + `game.c` + `bot.c` + `store.c` | none |

`CMakeLists.txt` picks the backend automatically based on the platform.

```bash
cmake -S . -B build
cmake --build build
```

The executable is written to the **project root**.

> **Keep the directory name pure ASCII.** With non-ASCII characters in the path, MinGW's `ld.exe` encodes filenames using the system ANSI code page and fails, so nothing links at all (Cygwin's ld does not have this problem).

## Controls

| Screen | Keyboard | Mouse |
|---|---|---|
| Main menu | ↑↓ to move, Enter to confirm | click an item to run it |
| Difficulty | ↑↓ to move, Enter to confirm | same |
| Dice roll | Enter to roll → Enter to start | click anywhere = Enter |
| Board | arrows to move, Enter/Space to place, `r` to restart, `q` back to menu | click a cell to place |
| Records | any key to go back | click anywhere to go back |

"Continue" is **dimmed and unselectable when there is no save**. If the terminal is smaller than 15 × 13, the game shows "terminal too small, please enlarge the window".

## Save file

The save and the records live in `tictactoe.dat` **next to the executable**, in a plain-ASCII `key value` line format:

```
white_wins 3
black_wins 2
draws 1
bot_wins 1
bot_losses 4
bot_draws 2
save 1
cur 1
cells 0 0 0 0 1 0 0 0 2
save_vs_bot 1
save_bot_side 2
save_level 1
```

Unrecognised keys are skipped and missing keys fall back to defaults, so **a corrupted file won't crash the game**, and adding new fields won't invalidate old saves. The file is rewritten after every move; when a game ends the result is added to the records and the save is cleared.

To wipe your records, just delete the file.

## Project layout

```
game.h / game.c   Game rules: board, win/draw detection, turn switching
bot.h / bot.c     Bot move selection (simple heuristic + minimax)
store.h / store.c Save file and records I/O
main.c            ncurses backend
win32_main.c      Win32 console backend
```

The first three are pure logic with no UI dependency and are shared by both backends; the last two are each compiled by only one platform. The two backends deliberately **mirror each other** (`layout` / `draw_board` / `draw_cells` / `draw_status` / `draw_help` line up one-to-one).

## Platform notes

- **Mouse**: the Win32 backend works in both cmd.exe and Windows Terminal. The **ncurses backend only works under mintty** — when a Cygwin program runs inside cmd.exe or Windows Terminal, Cygwin uses the `cygwin` terminfo entry, which has no `kmous` capability. The game checks this and only shows the "mouse works here" hint in the status line when the terminal actually supports it, so if you don't see the hint, the current terminal doesn't.
- **Terminal encoding**: the ncurses build needs a UTF-8 terminal with colour support, otherwise it exits with "this terminal does not support colours".
- **Windows console**: the Win32 build disables Quick Edit mode, otherwise mouse clicks are swallowed by the console as text selection. The trade-off is that you can no longer select and copy text with the mouse in that window.
