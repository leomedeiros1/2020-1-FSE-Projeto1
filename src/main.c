#include <stdio.h>
#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <linux_userspace.c>

#include <uart_utils.h>

#define MIN_ROWS 24
#define MIN_COLS 90

#define KEYBOARD_INPUT 0
#define POTENCIOMETER_INPUT 1
// Commands
#define CMD_EXIT 49 //1
#define CMD_KEYBOARD_INPUT 50 // 2
#define CMD_POTENCIOMETER_INPUT 51 //3 
#define CMD_SET_HISTERESIS 52 // 4

static const char I2C_PATH[] = "/dev/i2c-1";

struct bme280_dev dev;

bool running = true;
int input_mode = KEYBOARD_INPUT;

float extern_temp;
float intern_temp;
float reference_temp;
bool reference_temp_ready = false;
float histeresis_temp;
bool histeresis_temp_ready = false;
float potenciometer;

pthread_t keyboard_thread;
pthread_t sensors_thread;

void *watchKeyboard(void *args);
void *watchSensors(void *args);

void printMenu(WINDOW *menuWindow);

void initI2C();

int main(){
    // Initialize BME280
    initI2C();

    // Initialize ncurses
    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    refresh();

    int rows, columns;
    getmaxyx(stdscr, rows, columns);
    while(rows < MIN_ROWS || columns < MIN_COLS){
        mvprintw(0, 0, "Seu terminal é muito pequeno para usar este programa, por favor reajuste");
        mvprintw(1, 0, "Minimos: %d linhas e %d colunas", MIN_ROWS, MIN_COLS);
        mvprintw(2, 0, "Atual: %d linhas e %d colunas", rows, columns);
        refresh();
        usleep(500000);
        getmaxyx(stdscr, rows, columns);
        clear();
        refresh();
    }

    WINDOW *sensorsWindow = newwin(LINES - 12, COLS, 0, 0);
    WINDOW *menuWindow = newwin(8, COLS, LINES - 12, 0);
    WINDOW *inputWindow = newwin(4, COLS, LINES - 4, 0);

    printMenu(menuWindow);

    if(pthread_create(&keyboard_thread, NULL, watchKeyboard, (void *) inputWindow)){
        endwin();
        printf("ERRO: Falha na criacao de thread(1)\n");
        return -1;
    }


    if(pthread_create(&sensors_thread, NULL, watchSensors, (void *) sensorsWindow)){
        endwin();
        printf("ERRO: Falha na criacao de thread(2)\n");
        return -2;
    } 

    pthread_join(keyboard_thread, NULL);

    delwin(sensorsWindow);
    delwin(menuWindow);
    delwin(inputWindow);
    echo();
    endwin();

    return 0;
}

void printMenu(WINDOW *menuWindow){
    box(menuWindow, 0, 0);
    wrefresh(menuWindow);
    mvwprintw(menuWindow, 1, 1, "Lista de comandos disponíveis:");
    mvwprintw(menuWindow, 2, 1, "2 - Definir temperatura de referência manualmente");
    mvwprintw(menuWindow, 3, 1, "3 - Definir temperatura via potenciômetro");
    mvwprintw(menuWindow, 4, 1, "4 - Definir temperatura de histerese");
    mvwprintw(menuWindow, 6, 1, "1 - Sair");
    wrefresh(menuWindow);
}

void *watchKeyboard(void *args){
    WINDOW *inputWindow = (WINDOW *) args;
    int op_code;
    box(inputWindow, 0, 0);
    wrefresh(inputWindow);
    while((op_code = getch()) != CMD_EXIT){
        // mvprintw(1, 1, "> %d", op_code);
        switch(op_code){
            case CMD_KEYBOARD_INPUT:{
                input_mode = KEYBOARD_INPUT;
                float new_temperature=0.0f;

                echo();

                mvwprintw(inputWindow, 1, 1, "Insira a nova temperatura de referência desejada");
                mvwprintw(inputWindow, 2, 1, "> ");
                wscanw(inputWindow, "%f", &new_temperature);
                reference_temp = new_temperature;
                reference_temp_ready=true;

                noecho();

                break;
            }
            case CMD_POTENCIOMETER_INPUT:{
                input_mode = POTENCIOMETER_INPUT;
                break;
            }
            case CMD_SET_HISTERESIS:{
                float new_histeresis=0.0f;

                echo();

                mvwprintw(inputWindow, 1, 1, "Insira a nova temperatura de histerese desejada");
                mvwprintw(inputWindow, 2, 1, "> ");
                wscanw(inputWindow, "%f", &new_histeresis);
                histeresis_temp = new_histeresis;
                histeresis_temp_ready = true;

                noecho();
                break;
            }
        }
        if(histeresis_temp_ready && histeresis_temp_ready){
            running=true;
        }
        wclear(inputWindow);
        box(inputWindow, 0, 0);
        wrefresh(inputWindow);
    }
    return NULL;
}

void *watchSensors(void *args){
    WINDOW *sensorsWindow = (WINDOW *) args;
    while(true){
        wclear(sensorsWindow);
        box(sensorsWindow, 0, 0);
        wrefresh(sensorsWindow);
        if(running){
            // get_sensor_data()
            float _temp;
            int res = getTI(&_temp);
            // if (res)
            if(input_mode == POTENCIOMETER_INPUT){
                res = getTR(&_temp);
                // if (!res)
                //   mvwprintw(sensorsWindow, 2, 1, "Temperatura referencia %.2f oC", _temp);
            }
            // print_sensors()
            // clrtoeol();
            mvwprintw(sensorsWindow, 1, 1, "Retorno %d", res);
            if(!res){
                mvwprintw(sensorsWindow, 3, 1, "Temperatura interna %.2f oC", _temp);
                // wrefresh(sensorsWindow);
            }
            // int rslt = get_sensor_data_forced_mode(&dev, &_temp);
            
            // if (rslt != BME280_OK){
            //     // fprintf(stderr, "Failed to stream sensor data (code %+d).\n", rslt);
            //     exit(1);
            // }
            mvwprintw(sensorsWindow, 4, 1, "Temperatura externa %.2f oC", 25.5);
            wrefresh(sensorsWindow);
            usleep(200000);
        }else{
            sleep(1);
        }
    }
}

void initI2C(){
    struct identifier id;
    if ((id.fd = open(I2C_PATH, O_RDWR)) < 0) {
        fprintf(stderr, "Falha na abertura do canal I2C %s\n", I2C_PATH);
        exit(2);
    }
    id.dev_addr = BME280_I2C_ADDR_PRIM;
    if (ioctl(id.fd, I2C_SLAVE, id.dev_addr) < 0) {
        fprintf(stderr, "Falha na comunicaçaõ I2C\n");
        exit(3);
    }
    dev.intf = BME280_I2C_INTF;
    dev.read = user_i2c_read;
    dev.write = user_i2c_write;
    dev.delay_us = user_delay_us;
    dev.intf_ptr = &id;
    int rslt = bme280_init(&dev);
    if (rslt != BME280_OK) {
        fprintf(stderr, "Falha na inicialização do dispositivo(codigo %+d).\n", rslt);
        exit(4);
    }
}