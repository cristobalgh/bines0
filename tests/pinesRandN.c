#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
    int pines[] = {2, 5, 6, 8};//7, 11, 12, 15
    int i, mask, tiempo;

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
        // Generar patrón aleatorio de 1 a 15 (0001 a 1111 en binario)
        mask = (rand() % 15) + 1; 

        // Aplicar el patrón a los pines
        for (i = 0; i < 4; i++) {
            if (mask & (1 << i))
                digitalWrite(pines[i], HIGH);
            else
                digitalWrite(pines[i], LOW);
        }

        // Tiempo encendido aleatorio entre 100 y 1000 ms
        tiempo = 100 + rand() % 901;
        delay(tiempo);

        // Apagar todos
        for (i = 0; i < 4; i++) {
            digitalWrite(pines[i], LOW);
        }

        // Pausa aleatoria entre 100 y 500 ms
        delay(100 + rand() % 401);
    }

    return 0;
}
