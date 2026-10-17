cmake_minimum_required(VERSION 4.2)
project(TicTacToe C)

set(CMAKE_C_STANDARD 23)

# 可执行文件直接产出到项目根目录，方便找
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

if(WIN32 AND NOT CYGWIN)
    # 原生 Windows：直接用 Win32 控制台 API，无第三方依赖，
    # 产出的单个 exe 可以直接发给别人运行
    add_executable(TicTacToe win32_main.c game.c store.c bot.c)
else()
    # Cygwin / Unix：ncurses
    # CURSES_NEED_WIDE 必须：否则会选中窄字符版 libcurses，制表符和中文都画不出来
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)
    add_executable(TicTacToe main.c game.c store.c bot.c)
    target_include_directories(TicTacToe PRIVATE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(TicTacToe PRIVATE ${CURSES_LIBRARIES})
endif()
