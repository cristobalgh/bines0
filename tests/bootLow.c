#include <wiringPi.h>
#include <stdio.h>

int main(void) {
    int pines[] = {2, 5, 6, 8};
    int i;

    if (wiringPiSetup() == -1) {
        printf("Error inicializando wiringPi\n");
        return 1;
    }

    // Configurar pines como salida
    for (i = 0; i < 4; i++) {
        pinMode(pines[i], OUTPUT);
        digitalWrite(pines[i], LOW);
    }
    for (i = 15; i < 17; i++) {
        pinMode(i, INPUT);
    }


/*    while (1) {
        // Encender de a uno
        for (i = 0; i < 4; i++) {
            digitalWrite(pines[i], HIGH);
            delay(500); // medio segundo
            digitalWrite(pines[i], LOW);
        }
    }

*/
    return 0;
}
