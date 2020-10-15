#include <stdio.h>
#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <termios.h>
#include <fcntl.h>
#include <linux_userspace.c>
#include <signal.h>
#include <bcm2835.h>
#include <semaphore.h>

#include <uart_utils.h>
#include <i2clcd.h>

#define MIN_ROWS 24
#define MIN_COLS 90

#define KEYBOARD_INPUT 0
#define POTENTIOMETER_INPUT 1

#define ST_STAND_BY 0
#define ST_WARMING_UP 1
#define ST_COOLING_DOWN 2
// Commands
#define CMD_EXIT 48 // 0
#define CMD_KEYBOARD_INPUT 49 // 1
#define CMD_POTENTIOMETER_INPUT 50 // 2 
#define CMD_SET_HISTERESIS 51 // 3

static const char I2C_PATH[] = "/dev/i2c-1";
static const char CSV_DATA_PATH[] = "./data.csv";

struct bme280_dev dev;

bool running = false;
int input_mode = KEYBOARD_INPUT;
int state = ST_STAND_BY;
int time_it = 0;

float extern_temp;
float intern_temp;
float reference_temp;
bool reference_temp_ready = false;
float histeresis_temp;
bool histeresis_temp_ready = false;
float potentiometer;

pthread_t keyboard_thread;
pthread_t sensors_thread;
pthread_t log_thread;
pthread_t lcd_thread;
pthread_t control_thread;

sem_t hold_sensors;
sem_t hold_logger;
sem_t hold_lcd;
sem_t hold_control;

void *watchKeyboard(void *args);
void *watchSensors(void *args);
void *handleCSV(void *args);
void *handleLCD(void *args);
void *handleGPIO(void *args);

void printMenu(WINDOW *menuWindow);
void printData(WINDOW *sensorsWindow);
void writeCSV();

void handleAlarm(int signal);

int startThreads(WINDOW *inputWindow, WINDOW *sensorsWindow);

void safeExit(int signal);

int main(){
    // Initialize Alarm
    signal(SIGALRM, handleAlarm);
    ualarm(500000, 500000);

    // Initialize semaphores
    sem_init(&hold_sensors, 0, 0);
    sem_init(&hold_logger, 0, 0);
    sem_init(&hold_lcd, 0, 0);

    // Add signals to safe exit
    signal(SIGKILL, safeExit);
    signal(SIGSTOP, safeExit);
    signal(SIGINT, safeExit);
    signal(SIGTERM, safeExit);

    // Initialize i2clcd
    lcd_init();

    // Initialize BME280
    struct identifier id;
    if((id.fd = open(I2C_PATH, O_RDWR)) < 0) {
        endwin();
        fprintf(stderr, "Falha na abertura do canal I2C %s\n", I2C_PATH);
        exit(2);
    }
    id.dev_addr = BME280_I2C_ADDR_PRIM;
    if(ioctl(id.fd, I2C_SLAVE, id.dev_addr) < 0) {
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
    if(rslt != BME280_OK) {
        endwin();
        fprintf(stderr, "Falha na inicialização do dispositivo(codigo %+d).\n", rslt);
        exit(4);
    }

    // Initialize bcm2835
    if(!bcm2835_init()){
        fprintf(stderr, "Erro na inicialização do bcm2835\n");
        exit(5);
    };
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_18, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(RPI_V2_GPIO_P1_16, BCM2835_GPIO_FSEL_OUTP);

    // Initialize ncurses
    initscr();
    cbreak();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0);
    refresh();

    // Verify window size
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

    startThreads(inputWindow, sensorsWindow);

    pthread_join(keyboard_thread, NULL);

    delwin(sensorsWindow);
    delwin(menuWindow);
    delwin(inputWindow);

    safeExit(0);
    return 0;
}

void handleAlarm(int signal){
    sem_post(&hold_sensors);
    sem_post(&hold_logger);
    sem_post(&hold_lcd);
    sem_post(&hold_control);
}

int startThreads(WINDOW *inputWindow, WINDOW *sensorsWindow){
    if(pthread_create(&keyboard_thread, NULL, watchKeyboard, (void *) inputWindow)){
        endwin();
        fprintf(stderr, "ERRO: Falha na criacao de thread(1)\n");
        exit(-1);
    }

    if(pthread_create(&sensors_thread, NULL, watchSensors, (void *) sensorsWindow)){
        endwin();
        fprintf(stderr, "ERRO: Falha na criacao de thread(2)\n");
        exit(-2);
    } 

    if(pthread_create(&log_thread, NULL, handleCSV, NULL)){
        endwin();
        fprintf(stderr, "ERRO: Falha na criacao de thread(3)\n");
        exit(-3);
    }

    if(pthread_create(&lcd_thread, NULL, handleLCD, NULL)){
        endwin();
        fprintf(stderr, "ERRO: Falha na criacao de thread(4)\n");
        exit(-4);
    }

    if(pthread_create(&lcd_thread, NULL, handleGPIO, NULL)){
        endwin();
        fprintf(stderr, "ERRO: Falha na criacao de thread(5)\n");
        exit(-5);
    }

    return 0;
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
            case CMD_POTENTIOMETER_INPUT:{
                input_mode = POTENTIOMETER_INPUT;
                reference_temp_ready = true;
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
        if(histeresis_temp_ready && reference_temp_ready){
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
        sem_wait(&hold_sensors);
        wclear(sensorsWindow);
        box(sensorsWindow, 0, 0);
        wrefresh(sensorsWindow);

        printData(sensorsWindow);
        
        if(running){
            // get_sensor_data()
            float _temp;
            int res = getTI(&_temp);
            if (!res){
                intern_temp = _temp;
            }
            if(input_mode == POTENTIOMETER_INPUT){
                res = getTR(&_temp);
                if (!res){
                    reference_temp = _temp;
                }
            }else{
                // usleep(250000);
            }
            
            mvwprintw(sensorsWindow, 1, 1, "Retorno %d", res);
            
            int rslt = get_sensor_data_forced_mode(&dev, &_temp);
            if (rslt == BME280_OK){
                extern_temp = _temp;
                reference_temp_ready = true;
            }else{
                endwin();
                fprintf(stderr, "Falha na leitura do sensor BME280 (code %+d).\n", rslt);
                exit(1);
            }

            // // handleGPIO();
        }else{
            // usleep(500000);
        }
    }
}

void *handleCSV(void *args){
    while(true){
        sem_wait(&hold_logger);
        if(++time_it == 4){
            time_it=0;
            writeCSV();
        }
    }

    return NULL;
}

void *handleLCD(void *args){
    while(true){
        sem_wait(&hold_lcd);
        char STR_LINE1[16] = "";
        sprintf(STR_LINE1, "TR %.2f ", reference_temp);
        lcdLoc(LINE1);
        typeln(STR_LINE1);

        char STR_LINE2[16] = "";
        sprintf(STR_LINE2, "TI%.2f TE%.2f ", intern_temp, extern_temp);
        lcdLoc(LINE2);
        typeln(STR_LINE2);

    }
    return NULL;
}

void *handleGPIO(void *args){
    while(true){
        sem_wait(&hold_control);
        // Controle
        float histeresis_var = histeresis_temp / 2;
        if(intern_temp < reference_temp - histeresis_var){
            state = ST_WARMING_UP;
            // liga resistor
            bcm2835_gpio_write(RPI_V2_GPIO_P1_16, 0);
            // desliga ventilador
            bcm2835_gpio_write(RPI_V2_GPIO_P1_18, 1);
        }else if(intern_temp > reference_temp + histeresis_var){
            state = ST_COOLING_DOWN;
            // desliga resistor
            bcm2835_gpio_write(RPI_V2_GPIO_P1_16, 1);
            // liga ventilador
            bcm2835_gpio_write(RPI_V2_GPIO_P1_18, 0);
        }else if(intern_temp < reference_temp){
            state = ST_STAND_BY;
            // desliga ventilador
            bcm2835_gpio_write(RPI_V2_GPIO_P1_18, 1);
        }else if(intern_temp > reference_temp){
            // desliga resistor
            state = ST_STAND_BY;
            bcm2835_gpio_write(RPI_V2_GPIO_P1_16, 1);
        }
    }
    return NULL;
}

void safeExit(int signal){
    pthread_cancel(sensors_thread);
    pthread_cancel(keyboard_thread);
    pthread_cancel(log_thread);
    pthread_cancel(lcd_thread);
    pthread_cancel(control_thread);

    // Turn actuators off
    bcm2835_gpio_write(RPI_V2_GPIO_P1_18, 1); // Cooler
    bcm2835_gpio_write(RPI_V2_GPIO_P1_16, 1); // Resist

    echo();
    endwin();

    if(signal){
        printf("Execução abortada pelo signal: %d\n", signal);
    }else{
        printf("Execução finalizada pelo usuário\n");
    }

    exit(signal);
}

void writeCSV(){
    // Open csv
    FILE *arq;
    arq = fopen(CSV_DATA_PATH, "r+");
    if(arq){
        fseek(arq, 0, SEEK_END);
    }
    else{
        arq = fopen("./data.csv", "a");
        // Header
        fprintf(arq, "Temperatura referência (oC), Temperatura interna (oC), Temperatura externa (oC), Data e Hora\n");
    }

    if(arq){
        time_t rawtime;
        struct tm * timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        fprintf(arq, "%0.2lf, %0.2lf, %0.2lf, %s", reference_temp, intern_temp, extern_temp, asctime (timeinfo));
    }
    else{
        endwin();
        printf("Não foi possivel abrir o arquivo para csv.\n");
        exit(-1);
    }

    // Close CSV
    fclose(arq); 
}

void printMenu(WINDOW *menuWindow){
    box(menuWindow, 0, 0);
    wrefresh(menuWindow);
    mvwprintw(menuWindow, 1, 1, "Lista de comandos disponíveis:");
    mvwprintw(menuWindow, 2, 1, "1 - Definir temperatura de referência manualmente");
    mvwprintw(menuWindow, 3, 1, "2 - Definir temperatura de referência via potenciômetro");
    mvwprintw(menuWindow, 4, 1, "3 - Definir temperatura de histerese");
    mvwprintw(menuWindow, 6, 1, "0 ou CTRL+C - Sair");
    wrefresh(menuWindow);
}

void printData(WINDOW *sensorsWindow){
    if(running){
        mvwprintw(sensorsWindow, 1, 1, "> Executando");
        if(state==ST_STAND_BY){
            mvwprintw(sensorsWindow, 2, 1, "Status: Dentro da temperatura de histerese");
        }else if(state==ST_WARMING_UP){
            mvwprintw(sensorsWindow, 2, 1, "Status: Aquecendo");
        }else if(state==ST_COOLING_DOWN){
            mvwprintw(sensorsWindow, 2, 1, "Status: Resfriando");
        }else{
            mvwprintw(sensorsWindow, 2, 1, "Status: ?????????");
        }
    }else if(reference_temp_ready){
        mvwprintw(sensorsWindow, 1, 1, "> Aguardando definição da temperatura de Histerese");
    }else if(histeresis_temp_ready){
        mvwprintw(sensorsWindow, 1, 1, "> Aguardando definição da temperatura de referência");
    }else{
        mvwprintw(sensorsWindow, 1, 1, "> Aguardando definição das variáveis de controle");
    }
    if(input_mode == KEYBOARD_INPUT){
        mvwprintw(sensorsWindow, 3, 1, "TR: Temperatura de referência definida manualmente");
    }else{
        mvwprintw(sensorsWindow, 3, 1, "TR: Temperatura de referência definida via potenciômetro");
    }
    if(reference_temp_ready){
        mvwprintw(sensorsWindow, 4, 1, "Temperatura de referência: %.2f oC", reference_temp);
    }else{
        mvwprintw(sensorsWindow, 4, 1, "Temperatura de referência: Não definida");
    }
    if(histeresis_temp_ready){
        mvwprintw(sensorsWindow, 5, 1, "Histerese do sistema: %.2f oC", histeresis_temp);
    }else{
        mvwprintw(sensorsWindow, 5, 1, "Histerese do sistema: Não definida");
    }

    mvwprintw(sensorsWindow, 6, 1, "Temperatura interna %.2f oC", intern_temp);
    mvwprintw(sensorsWindow, 7, 1, "Temperatura externa %.2f oC", extern_temp);
    wrefresh(sensorsWindow);
}