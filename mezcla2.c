#include <wiringPi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <wiringSerial.h>
#include <sys/stat.h>
#include <errno.h>

#define VERSION "1.3.0"
#define BOMBA1      2
#define BOMBA2      5
#define VALV1       6
#define VALV2       8
#define BUFFER_SIZE 512

#define PORC_AGUA 0.375
#define PORC_AFS40 0.625

int serialFd;

// Apagar todas las salidas
void apagarTodo() {
    digitalWrite(BOMBA1, LOW);
    digitalWrite(BOMBA2, LOW);
    digitalWrite(VALV1, LOW);
    digitalWrite(VALV2, LOW);
    printf("\n** TODAS LAS SALIDAS APAGADAS **\n");
}

// Manejo de Ctrl+C
void manejarCtrlC(int sig) {
    (void)sig;
    apagarTodo();
    if(serialFd > 0) close(serialFd);
    printf("Programa terminado por usuario (Ctrl+C), todas las salidas apagadas\n");
    exit(0);
}

// Configuración del puerto serie
int configurarSerial(const char *puerto) {
    int fd = open(puerto, O_RDWR | O_NOCTTY | O_NDELAY);
    if(fd < 0) { perror("Error abriendo puerto"); return -1; }

    struct termios tty;
    tcgetattr(fd, &tty);

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

int leerPesoSerial() {
    static char buffer[BUFFER_SIZE];
    int idx = 0;
    char c;
    int intentos = 0;

    while (1) {
        if (serialDataAvail(serialFd) > 0) {
            c = serialGetchar(serialFd);
            if (c == '\r') continue;
            if (c == '\n') {
                buffer[idx] = '\0';
                break;
            }
            if (idx < BUFFER_SIZE - 1) buffer[idx++] = c;
        } else {
            usleep(1000);
            intentos++;
            if (intentos > 5000) { // timeout ~5s
                printf("Timeout leyendo balanza\n");
                return -1;
            }
        }
    }

    char *ptr = strchr(buffer, '+');
    if (!ptr) ptr = strchr(buffer, '-');
    if (!ptr) return 0;

    ptr++;
    while (*ptr == ' ') ptr++;

    char numero[16];
    int i = 0;
    while ((*ptr >= '0' && *ptr <= '9') && i < 15) {
        numero[i++] = *ptr;
        ptr++;
    }
    numero[i] = '\0';

    return atoi(numero);
}

// Llenado de cada etapa
void llenarEtapa(float objetivo, float *kgActual, float *kgFluido, int pinBomba, int pinValvula) {
    digitalWrite(pinBomba, HIGH);
    digitalWrite(pinValvula, HIGH);

    if(pinBomba == BOMBA1)
        printf("Bomba y válvula del agua encendidas (BOMBA1=%d, VALV1=%d)\n\n", pinBomba, pinValvula);
    else
        printf("Bomba y válvula del ASF40 encendidas (BOMBA2=%d, VALV2=%d)\n\n", pinBomba, pinValvula);

    float pesoInicio = leerPesoSerial();
    float objetivoEtapa = pesoInicio + objetivo;
    float pesoCambio = pesoInicio;

    while(*kgActual + 0.5 < objetivoEtapa) {
        float peso = leerPesoSerial();
        if(peso >= -100 && peso <= 2000) {
            *kgActual = peso;
            *kgFluido = *kgActual - pesoInicio;
            if(peso != pesoCambio){
                printf("Peso actual: %.0f kg (Etapa: %.0f kg)\n", *kgActual, *kgFluido);
                pesoCambio = peso;
            }
        }
    }

    digitalWrite(pinBomba, LOW);
    digitalWrite(pinValvula, LOW);
    printf("\nEtapa completada. Actual total: %.0f kg\n", *kgActual);
}

// Guardar resultados
void guardarResultados(float kgAgua, float kgAFS40, float kgTotal) {
    struct stat st = {0};
    if (stat("mezclas", &st) == -1) {
        if (mkdir("mezclas", 0777) != 0) {
            printf("Error al crear carpeta 'mezclas': %s\n", strerror(errno));
            return;
        }
    }

    char nombreArchivo[128];
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    strftime(nombreArchivo, sizeof(nombreArchivo), "mezclas/mezcla_%Y%m%d_%H%M%S.txt", tm_info);

    FILE *archivo = fopen(nombreArchivo, "w");
    if(!archivo) { 
        printf("Error al crear archivo '%s': %s\n", nombreArchivo, strerror(errno));
        return; 
    }

    char fechaStr[64];
    strftime(fechaStr, sizeof(fechaStr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(archivo,
        "Reporte de mezcla\n"
        "-----------------\n"
        "Fecha: %s\n"
        "Agua (bomba 1): %.1f kg\n"
        "AFS40 (bomba 2): %.1f kg\n"
        "Mezcla total: %.1f kg\n",
        fechaStr, kgAgua, kgAFS40, kgTotal);

    fclose(archivo);
    printf("\nResultados guardados en archivo: %s\n", nombreArchivo);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, manejarCtrlC);
    printf("\n=== Programa de mezcla de fluidos ===\n");
    printf("Versión: %s\n", VERSION);

    if(wiringPiSetup() == -1) { printf("Error iniciando WiringPi\n"); return 1; }

    pinMode(BOMBA1, OUTPUT);
    pinMode(BOMBA2, OUTPUT);
    pinMode(VALV1, OUTPUT);
    pinMode(VALV2, OUTPUT);

    serialFd = configurarSerial("/dev/ttyUSB0");
    if(serialFd == -1) return 1;

    if(argc < 2) {
        printf("Uso: %s <kg_objetivo>\n", argv[0]);
        return 1;
    }

    float kgObjetivo = atof(argv[1]);
    if(kgObjetivo <= 0 || kgObjetivo > 1500) {
        printf("Error: kg objetivo debe estar entre 0 y 1500.\n");
        return 1;
    }

    printf("\nValor aceptado: %.0f kg\n\n", kgObjetivo);

    float kgActual = 0, kgAgua = 0, kgAFS40 = 0;

    // Etapa 1: Agua
    llenarEtapa(kgObjetivo * PORC_AGUA, &kgActual, &kgAgua, BOMBA1, VALV1);

    // Etapa 2: AFS40
    llenarEtapa(kgObjetivo * PORC_AFS40, &kgActual, &kgAFS40, BOMBA2, VALV2);

    printf("\nProceso terminado.\n");
    printf("Agua: \t\t%.0f kg\n", kgAgua);
    printf("AFS40: \t\t%.0f kg\n", kgAFS40);
    printf("Mezcla total:\t%.0f kg\n", kgAgua+kgAFS40);

    guardarResultados(kgAgua, kgAFS40, kgActual);

    printf("\ncgh 8.25 ");
    srand(time(NULL));
    for(int i = 0; i < 4; i++) {
        char c;
        int tipo = rand() % 3;
        if (tipo == 0) c = '0' + rand() % 10;
        else if (tipo == 1) c = 'A' + rand() % 26;
        else c = 'a' + rand() % 26;
        printf("%c", c);
    }
    printf("\n\n");

    close(serialFd);
    return 0;
}
