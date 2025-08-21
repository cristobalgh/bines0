#include <wiringPi.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

#define BOMBA1  2   // Pin bomba 1 (WiringPi)
#define VALV1   6   // Pin válvula 1 (WiringPi)

volatile bool salir = false;

// Manejo de Ctrl+C
void manejadorSIGINT(int signo) {
    salir = true;
}

// Apagar todo
void apagarTodo() {
    digitalWrite(BOMBA1, LOW);
    digitalWrite(VALV1, LOW);
    printf("\nTodo apagado.\n");
}

int main() {
    // Inicializar WiringPi
    if (wiringPiSetup() == -1) {
        printf("Error iniciando WiringPi\n");
        return 1;
    }

    // Configuración de pines
    pinMode(BOMBA1, OUTPUT);
    pinMode(VALV1, OUTPUT);

    // Registrar manejador de Ctrl+C
    signal(SIGINT, manejadorSIGINT);

    // Encender bomba y válvula
    digitalWrite(BOMBA1, HIGH);
    digitalWrite(VALV1, HIGH);
    printf("Bomba 1 y válvula 1 encendidas. Presiona Ctrl+C para detener.\n");

    // Loop principal (espera Ctrl+C)
    while (!salir) {
        delay(200);
    }

    // Apagar todo antes de salir
    apagarTodo();
    return 0;
}
