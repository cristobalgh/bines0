#include <wiringPi.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>

#define BOMBA2   5   // Pin bomba 2 (WiringPi)
#define VALV2    8   // Pin válvula 2 (WiringPi)

volatile bool salir = false;

// Manejo de Ctrl+C
void manejadorSIGINT(int signo) {
    salir = true;
}

// Apagar todo
void apagarTodo() {
    digitalWrite(BOMBA2, LOW);
    digitalWrite(VALV2, LOW);
    printf("\nTodo apagado.\n");
}

int main() {
    // Inicializar WiringPi
    if (wiringPiSetup() == -1) {
        printf("Error iniciando WiringPi\n");
        return 1;
    }

    // Configuración de pines
    pinMode(BOMBA2, OUTPUT);
    pinMode(VALV2, OUTPUT);

    // Registrar manejador de Ctrl+C
    signal(SIGINT, manejadorSIGINT);

    // Encender bomba y válvula
    digitalWrite(BOMBA2, HIGH);
    digitalWrite(VALV2, HIGH);
    printf("Bomba 2 y válvula 2 encendidas. Presiona Ctrl+C para detener.\n");

    // Loop principal (espera Ctrl+C)
    while (!salir) {
        delay(200);
    }

    // Apagar todo antes de salir
    apagarTodo();
    return 0;
}
