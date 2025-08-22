#include <wiringPi.h>
#include <wiringSerial.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    int fd;

    // Abrir puerto serie /dev/ttyUSB0 a 9600 baudios
    if ((fd = serialOpen("/dev/ttyUSB0", 9600)) < 0) {
        fprintf(stderr, "No se puede abrir el puerto serie: %s\n", "/dev/ttyUSB0");
        return 1;
    }

    // Inicializar wiringPi
    if (wiringPiSetup() == -1) {
        fprintf(stderr, "No se pudo inicializar wiringPi\n");
        return 1;
    }

    printf("Enviando 'T\\r\\n' cada 3 segundos por /dev/ttyUSB0...\n");

    while (1) {
        serialPuts(fd, "T\r\n");  // Enviar T + CR + LF
        serialFlush(fd);         // Vaciar buffer de salida
        printf("T enviado con CR+LF\n");
        delay(3000);             // Esperar 3 segundos (3000 ms)
    }

    return 0;
}
