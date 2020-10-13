#include <stdio.h>
#include <ncurses.h>
#include <string.h>
#include <unistd.h>

#define MIN_ROWS 24
#define MIN_COLS 90

int main(){

    // Initialize ncurses
    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);

    int rows, columns;
    getmaxyx(stdscr, rows, columns);
    while(rows < MIN_ROWS || columns < MIN_COLS){
        mvprintw(0, 0, "Seu terminal é muito pequeno para usar este programa, por favor reajuste");
        mvprintw(1, 0, "Minimos: %d linhas e %d colunas", MIN_ROWS, MIN_COLS);
        mvprintw(2, 0, "Atual: %d linhas e %d colunas", rows, columns);
        refresh();
        usleep(2000000);
        getmaxyx(stdscr, rows, columns);
        clear();
        refresh();
        // getch();
    }


    endwin();

    return 0;
}