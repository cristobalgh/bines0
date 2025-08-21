#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
    int pines[] = {2, 5, 6, 8};
    int i, pin, tiempo;

    if (wiringPiSetup() == -1) {
        printf("Error inicializando wiringPi\n");
        return 1;
    }

    // Configurar pines como salida y apagados
    for (i = 0; i < 4; i++) {
        pinMode(pines[i], OUTPUT);
        digitalWrite(pines[i], LOW);
    }

    srand(time(NULL)); // Semilla aleatoria

    while (1) {
        // Elegir un pin al azar
        pin = rand() % 4;

        // Encenderlo
        digitalWrite(pines[pin], HIGH);

        // Tiempo encendido aleatorio entre 100 ms y 1000 ms
        tiempo = 100 + rand() % 901;
        delay(tiempo);

        // Apagarlo
        digitalWrite(pines[pin], LOW);

        // Pausa aleatoria entre 100 y 500 ms antes del siguiente
        delay(100 + rand() % 401);
    }

    return 0;
}
