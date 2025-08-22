#include <wiringPi.h>
#include <stdio.h>

int main(void) {
    int pines[] = {0, 1, 2, 3, 4, 5, 6};
    int i;

    if (wiringPiSetup() == -1) {
        printf("Error inicializando wiringPi\n");
        return 1;
    }

    // Configurar pines como salida
    for (i = 0; i < 7; i++) {
        pinMode(pines[i], OUTPUT);
        digitalWrite(pines[i], LOW);
    }

    while (1) {
        // Encender de a uno
        for (i = 0; i < 4; i++) {
            digitalWrite(pines[i], HIGH);
            delay(500); // medio segundo
            digitalWrite(pines[i], LOW);
        }
    }

    return 0;
}
