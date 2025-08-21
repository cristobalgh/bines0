#include <wiringPi.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

//la seleccion de pines es dado que parten apagados por defecto...
//al menos hasta nuevo aviso de armbian...

#define BOMBA1   2   // WiringPi pin bomba 1 (Agua) fisico 7
#define BOMBA2   5   // WiringPi pin bomba 2 (AFS40) fisico 11
#define VALV1    6   // WiringPi pin válvula 1 fisico 12
#define VALV2    8   // WiringPi pin válvula 2 fisico 15
#define FLUJO1   15  // WiringPi pin sensor flujo 1
#define FLUJO2   16  // WiringPi pin sensor flujo 2

#define PESO_POR_SEGUNDO 0.5 // kg/s (simulación tasa de llenado)
#define TIEMPO_PULSO_MS 200  // tiempo para cada "simulación" paso
#define TIEMPO_ESPERA_PULSO 10000 // ms, tiempo máximo para esperar pulso flujo

void apagarTodo() {
    digitalWrite(BOMBA1, LOW);
    digitalWrite(BOMBA2, LOW);
    digitalWrite(VALV1, LOW);
    digitalWrite(VALV2, LOW);
    printf("** TODO APAGADO POR ERROR **\n");
}

// Espera pulso en pinFlujo hasta n segundos. Retorna true si detecta pulso, false si timeout.
bool esperarPulsoConTimeout(int pinFlujo) {
    int estadoInicial = digitalRead(pinFlujo);
    printf("Esperando pulso en pin %d (máx %d ms)...\n", pinFlujo, TIEMPO_ESPERA_PULSO);

    int tiempoEsperado = 0;
    while (digitalRead(pinFlujo) == estadoInicial) {
        delay(10);
        tiempoEsperado += 10;
        if (tiempoEsperado >= TIEMPO_ESPERA_PULSO) {
            printf("ERROR: No se detectó pulso en %d ms\n", TIEMPO_ESPERA_PULSO);
            return false;
        }
    }
    printf("Pulso detectado. Iniciando llenado...\n");
    return true;
}

bool llenarEtapa(float objetivoParcial, float *kgActual, float *kgFluido, int pinBomba, int pinValvula, int pinFlujo) {
    digitalWrite(pinBomba, HIGH);
    digitalWrite(pinValvula, HIGH);
    printf("Bomba %d y válvula %d encendidas\n", pinBomba, pinValvula);

    if (!esperarPulsoConTimeout(pinFlujo)) {
        apagarTodo();
        return false;
    }

    int estadoAnterior = digitalRead(pinFlujo);

    while (*kgActual < objetivoParcial) {
        int estadoActual = digitalRead(pinFlujo);
        if (estadoActual != estadoAnterior) {
            printf("ERROR: Corte de flujo detectado en bomba %d\n", pinBomba);
            apagarTodo();
            return false;
        }
        *kgActual += PESO_POR_SEGUNDO * (TIEMPO_PULSO_MS / 1000.0);
        *kgFluido += PESO_POR_SEGUNDO * (TIEMPO_PULSO_MS / 1000.0);
        printf("kgActual total: %.2f\n", *kgActual);
        delay(TIEMPO_PULSO_MS);
    }

    digitalWrite(pinBomba, LOW);
    digitalWrite(pinValvula, LOW);
    printf("Etapa completada. kgActual: %.2f\n", *kgActual);
    return true;
}

void guardarResultados(float kgAgua, float kgAFS40, float kgTotal) {
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    char nombreArchivo[64];
    char fechaLarga[128];
    char hora[16];
    char contenido[512];

    strftime(nombreArchivo, sizeof(nombreArchivo), "mezcla_%Y%m%d_%H%M%S.txt", tm_info);
    strftime(fechaLarga, sizeof(fechaLarga), "%A %d %B %Y", tm_info);
    strftime(hora, sizeof(hora), "%H:%M:%S", tm_info);

    FILE *archivo = fopen(nombreArchivo, "w");
    if (archivo == NULL) {
        printf("Error al crear el archivo de resultados.\n");
        return;
    }

    snprintf(contenido, sizeof(contenido),
             "Reporte de mezcla\n"
             "-----------------\n"
             "Fecha: %s\n"
             "Hora: %s\n\n"
             "Agua (bomba 1): %.2f kg\n"
             "AFS40 (bomba 2): %.2f kg\n"
             "Mezcla total: %.2f kg\n",
             fechaLarga, hora, kgAgua, kgAFS40, kgTotal);

    fprintf(archivo, "%s", contenido);
    fclose(archivo);

    printf("Resultados guardados en archivo: %s\n", nombreArchivo);
}

int main() {
    float kgObjetivo, kgActual = 0;
    float kgAgua = 0, kgAFS40 = 0;

    if (wiringPiSetup() == -1) {
        printf("Error iniciando WiringPi\n");
        return 1;
    }

    pinMode(BOMBA1, OUTPUT);
    pinMode(BOMBA2, OUTPUT);
    pinMode(VALV1, OUTPUT);
    pinMode(VALV2, OUTPUT);
    pinMode(FLUJO1, INPUT);
    pinMode(FLUJO2, INPUT);

    pullUpDnControl(FLUJO1, PUD_UP);
    pullUpDnControl(FLUJO2, PUD_UP);

    printf("Ingrese cantidad de kg a preparar: ");
    scanf("%f", &kgObjetivo);

    if (!llenarEtapa(kgObjetivo * 0.625, &kgActual, &kgAgua, BOMBA1, VALV1, FLUJO1)) {
        return 1;
    }

    if (!llenarEtapa(kgObjetivo, &kgActual, &kgAFS40, BOMBA2, VALV2, FLUJO2)) {
        return 1;
    }

    printf("\nProceso terminado.\n");
    printf("Agua (bomba 1): %.2f kg\n", kgAgua);
    printf("AFS40 (bomba 2): %.2f kg\n", kgAFS40);
    printf("Mezcla total: %.2f kg\n", kgActual);

    guardarResultados(kgAgua, kgAFS40, kgActual);

    return 0;
}
