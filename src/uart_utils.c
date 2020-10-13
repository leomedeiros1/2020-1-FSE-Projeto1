#include <fcntl.h>
#include <termios.h>

static const char UART_PATH[] = "/dev/serial0";

int openUart(){
    int uart0 = -1;
    uart0 = open(UART_PATH, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart0 == -1){
        return -1;
    }
    struct termios options;
    tcgetattr(uart0, &options);
    options.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    options.c_iflag = IGNPAR;
    options.c_oflag = 0;
    options.c_lflag = 0;
    tcflush(uart0, TCIFLUSH);
    tcsetattr(uart0, TCSANOW, &options);

    return uart0;
}

int getData(float *data){
    int uart = openUart();

    if(uart != -1){
        char op_buffer[] = {0xA1, 8, 8, 9, 1}; // 170038891

        int res = write(uart, &op_buffer, sizeof(op_buffer));
        if (res < 0){
            close(uart);
            return -1;
        }

        usleep(250000);

        res = read(uart, (void *) data++, sizeof(float));
        if (res < 0){
            close(uart);
            return -2;
        }
        
        op_buffer[0] = 0xA2;

        res = write(uart, &op_buffer, sizeof(op_buffer));
        if (res < 0){
            close(uart);
            return -3;
        }

        usleep(250000);

        res = read(uart, (void *) data, sizeof(float));
        if (res < 0){
            close(uart);
            return -4;
        }

        close(uart);
    }

    return 0;
}