#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>

int configurarPuertoSerie(int fd) {
    struct termios tty;

    if(tcgetattr(fd, &tty) != 0) {
        perror("Error al obtener atributos del puerto");
        return -1;
    }

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    tty.c_cflag &= ~PARENB;        // Sin paridad
    tty.c_cflag &= ~CSTOPB;        // 1 bit de stop
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 bits por byte
    tty.c_cflag &= ~CRTSCTS;       // Sin control de flujo hardware
    tty.c_cflag |= CREAD | CLOCAL; // Activar lectura y ignorar control modem

    tty.c_lflag &= ~ICANON; // Modo raw, no line buffering
    tty.c_lflag &= ~ECHO;   // No echo
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ISIG;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Sin control de flujo software
    tty.c_iflag &= ~(INLCR | ICRNL | IGNCR);

    tty.c_oflag &= ~OPOST; // Modo raw output

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10; // Timeout lectura 1s

    if(tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error al configurar puerto");
        return -1;
    }
    return 0;
}

int main() {
    const char *puertoSerie = "/dev/ttyUSB0";  // Cambia según tu dispositivo
    int fd = open(puertoSerie, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("No se pudo abrir puerto serie");
        return 1;
    }

    if (configurarPuertoSerie(fd) != 0) {
        close(fd);
        return 1;
    }

    char buffer[256];
    int idx = 0;
    ssize_t n;
    char c;

    printf("Escuchando datos en %s...\n", puertoSerie);

    while (1) {
        n = read(fd, &c, 1);
        if (n > 0) {
            if (c == '\n') {
                buffer[idx] = '\0';
                printf("Recibido: %s\n", buffer);
                idx = 0;
            } else if (idx < (int)sizeof(buffer) - 1) {
                buffer[idx++] = c;
            }
        } else {
            // No hay datos, se puede hacer delay o continuar
            usleep(100000); // 100 ms
        }
    }

    close(fd);
    return 0;
}
