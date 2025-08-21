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

    tty.c_lflag &= ~ICANON; // Modo raw
    tty.c_lflag &= ~ECHO;   // No eco
    tty.c_lflag &= ~ECHOE;  // No erase
    tty.c_lflag &= ~ISIG;   // No señales

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
    const char *puertoSerie = "/dev/ttyUSB1";  // Cambia según tu dispositivo
    int fd = open(puertoSerie, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) {
        perror("No se pudo abrir puerto serie");
        return 1;
    }

    if (configurarPuertoSerie(fd) != 0) {
        close(fd);
        return 1;
    }

    char buffer[16];
    int i;
    while (1) {
        // De 1 a 100
        for(i = 1; i <= 100; i++) {
            int len = snprintf(buffer, sizeof(buffer), "%d\n", i);
            if (write(fd, buffer, len) < 0) {
                perror("Error al escribir puerto");
                close(fd);
                return 1;
            }
            printf("Enviado: %d\n", i);
            sleep(1);
        }
        // De 100 a 1
        for(i = 100; i >= 1; i--) {
            int len = snprintf(buffer, sizeof(buffer), "%d\n", i);
            if (write(fd, buffer, len) < 0) {
                perror("Error al escribir puerto");
                close(fd);
                return 1;
            }
            printf("Enviado: %d\n", i);
            sleep(1);
        }
    }

    close(fd);
    return 0;
}
