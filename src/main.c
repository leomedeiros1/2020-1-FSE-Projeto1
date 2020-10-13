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

#define ST_STAND_BY 0
#define ST_WARMING_UP 1
#define ST_COOLING_DOWN 2
// Commands
#define CMD_EXIT 49 //1
#define CMD_KEYBOARD_INPUT 50 // 2
#define CMD_POTENCIOMETER_INPUT 51 //3 
#define CMD_SET_HISTERESIS 52 // 4

static const char I2C_PATH[] = "/dev/i2c-1";

struct bme280_dev dev;

bool running = true;
int input_mode = KEYBOARD_INPUT;
int state = ST_STAND_BY;

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
void handleGPIO();

int main(){
    // Initialize BME280
    struct identifier id;
    if ((id.fd = open(I2C_PATH, O_RDWR)) < 0) {
        endwin();
        fprintf(stderr, "Falha na abertura do canal I2C %s\n", I2C_PATH);
        exit(2);
    }
    id.dev_addr = BME280_I2C_ADDR_PRIM;
    if (ioctl(id.fd, I2C_SLAVE, id.dev_addr) < 0) {
        endwin();
        fprintf(stderr, "Falha na comunicaçaõ I2C\n");
        exit(3);
    }
    dev.intf = BME280_I2C_INTF;
    dev.read = user_i2c_read;
    dev.write = user_i2c_write;
    dev.delay_us = user_delay_us;
    dev.intf_ptr = &id;
    int8_t rslt = bme280_init(&dev);
    if (rslt != BME280_OK) {
        endwin();
        fprintf(stderr, "Falha na inicialização do dispositivo(codigo %+d).\n", rslt);
        exit(4);
    }

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
            // print data()
            if(running){
                // mvwprintw(sensorsWindow, 1, 1, "Status: Executando");
            }else if(reference_temp_ready){
                // mvwprintw(sensorsWindow, 1, 1, "Status: Aguardando definição da temperatura de Histerese");
            }else if(histeresis_temp_ready){
                // mvwprintw(sensorsWindow, 1, 1, "Status: Aguardando definição da temperatura de referência");
            }else{
                // mvwprintw(sensorsWindow, 1, 1, "Status: Aguardando entrada do usuário");
            }
            mvwprintw(sensorsWindow, 3, 1, "Temperatura interna %.2f oC", intern_temp);
            mvwprintw(sensorsWindow, 4, 1, "Temperatura externa %.2f oC", extern_temp);
            mvwprintw(sensorsWindow, 5, 1, "Temperatura de referência %.2f oC", reference_temp);
            wrefresh(sensorsWindow);
            // get_sensor_data()
            float _temp;
            int res = getTI(&_temp);
            if (!res){
                intern_temp = _temp;
            }
            if(input_mode == POTENCIOMETER_INPUT){
                res = getTR(&_temp);
                if (!res){
                    reference_temp = _temp;
                    reference_temp_ready = true;
                }
            }
            
            mvwprintw(sensorsWindow, 1, 1, "Retorno %d", res);
            
            int rslt = get_sensor_data_forced_mode(&dev, &_temp);
            if (rslt == BME280_OK){
                extern_temp = _temp;
            }else{
                endwin();
                fprintf(stderr, "Falha na leitura do sensor BME280 (code %+d).\n", rslt);
                // exit(1);
            }
            
            // if (rslt != BME280_OK){
            //     
            // }
            usleep(500000);
            handleGPIO();
        }else{
            sleep(1);
        }
    }
}

void handleGPIO(){
    float histeresis_var = histeresis_temp / 2;

    if(intern_temp < reference_temp - histeresis_var){
        state = ST_WARMING_UP;
        // liga resistor
        // desliga ventilador
    }else if(intern_temp > reference_temp + histeresis_var){
        state = ST_COOLING_DOWN;
        // liga ventilador
        // desliga resistor
    }else if(intern_temp < reference_temp){
        state = ST_STAND_BY;
        // desliga ventilador
    }else if(intern_temp > reference_temp){
        // desliga resistor
        state = ST_STAND_BY;
    }
}