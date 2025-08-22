#include <wiringPi.h>
#include <stdio.h>

#define BOTON1 7
#define BOTON2 0
#define LED1   2
#define LED2   3

volatile int flagBoton1 = 0;
volatile int flagBoton2 = 0;

// Última vez que se presionó cada botón (ms)
volatile unsigned int lastPress1 = 0;
volatile unsigned int lastPress2 = 0;
const unsigned int DEBOUNCE_TIME = 200; // 200 ms

// ISR botón 1
void boton1Presionado(void) {
    unsigned int now = millis();
    if (now - lastPress1 > DEBOUNCE_TIME) {
        flagBoton1 = 1;
        lastPress1 = now;
    }
}

// ISR botón 2
void boton2Presionado(void) {
    unsigned int now = millis();
    if (now - lastPress2 > DEBOUNCE_TIME) {
        flagBoton2 = 1;
        lastPress2 = now;
    }
}

int main(void) {
    if (wiringPiSetup() == -1) {
        printf("Error al inicializar WiringPi\n");
        return 1;
    }

    // Configurar pines
    pinMode(BOTON1, INPUT);
    pinMode(BOTON2, INPUT);
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);

    pullUpDnControl(BOTON1, PUD_UP);
    pullUpDnControl(BOTON2, PUD_UP);

    // Configurar interrupciones
    if (wiringPiISR(BOTON1, INT_EDGE_FALLING, &boton1Presionado) < 0) {
        printf("Error al configurar ISR botón 1\n");
        return 1;
    }

    if (wiringPiISR(BOTON2, INT_EDGE_FALLING, &boton2Presionado) < 0) {
        printf("Error al configurar ISR botón 2\n");
        return 1;
    }

    printf("Esperando botones...\n");

    while (1) {
        // Revisar flags y encender LEDs
        if (flagBoton1) {
            printf("Botón 1 presionado!\n");
            digitalWrite(LED1, HIGH);
            delay(200); // tiempo visual
            digitalWrite(LED1, LOW);
            flagBoton1 = 0; // reset flag
        }

        if (flagBoton2) {
            printf("Botón 2 presionado!\n");
            digitalWrite(LED2, HIGH);
            delay(200);
            digitalWrite(LED2, LOW);
            flagBoton2 = 0;
        }

        // Otras tareas pueden ejecutarse aquí
        delay(50);
    }

    return 0;
}
