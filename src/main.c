#include <stdio.h>
#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define MIN_ROWS 24
#define MIN_COLS 90

#define KEYBOARD_INPUT 0
#define POTENCIOMETER_INPUT 1
// Commands
#define CMD_EXIT 49
#define CMD_KEYBOARD_INPUT 50
#define CMD_POTENCIOMETER_INPUT 51
#define CMD_SET_HISTERESIS 52


bool running = false;
int input_mode = KEYBOARD_INPUT;

pthread_t keyboard_thread;

void *watch_keyboard(void *args);

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

    if(pthread_create(&keyboard_thread, NULL, watch_keyboard, NULL)){
        endwin();
        
        printf("ERRO: Falha na criacao de thread(1)\n");
        
        return -1;
    }    

    pthread_join(keyboard_thread, NULL);

    endwin();

    return 0;
}

void *watch_keyboard(void *args){
    int op_code;
    while((op_code = getch()) != CMD_EXIT){
        mvprintw(1, 1, "> %d", op_code);
        switch(op_code){
            case CMD_KEYBOARD_INPUT:{
                input_mode = KEYBOARD_INPUT;
                float new_temperature=0.0f;

                echo();
                move(LINES-3, 0);
                clrtobot();

                printw("Insira a nova temperatura de referência desejada\n>");
                scanw("%f", &new_temperature);

                noecho();
                move(LINES-3, 0);
                clrtobot();

                running=true;
                break;
            }
            case CMD_POTENCIOMETER_INPUT:{
                input_mode = POTENCIOMETER_INPUT;
                break;
            }
            case CMD_SET_HISTERESIS:{
                float new_histeresis=0.0f;

                echo();
                move(LINES-3, 0);
                clrtobot();

                printw("Insira a nova temperatura de histerese desejada\n>");
                scanw("%f", &new_histeresis);

                move(LINES-3, 0);
                clrtobot();
                noecho();
                break;
            }
        }
    }
    return NULL;
}