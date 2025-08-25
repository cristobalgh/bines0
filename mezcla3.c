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

#define VERSION "1.3.2"
#define BOMBA1      2
#define BOMBA2      5
#define VALV1       6
#define VALV2       8
#define BUFFER_SIZE 512

#define PORC_AGUA  (3.0/8.0)
#define PORC_AFS40 (5.0/8.0)

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
    fflush(stdout);
    exit(0);
}

// Guardar error con pesos
void guardarError(const char *mensaje, float kgActual, float kgFluido) {
    struct stat st = {0};
    if (stat("mezclas", &st) == -1) {
        mkdir("mezclas", 0755);
    }

    char nombreArchivo[128];
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    strftime(nombreArchivo, sizeof(nombreArchivo), "mezclas/error_%Y%m%d_%H%M%S.txt", tm_info);

    FILE *archivo = fopen(nombreArchivo, "w");
    if(!archivo) return;

    char fechaStr[64];
    strftime(fechaStr, sizeof(fechaStr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(archivo,
        "ERROR DE PROCESO\n"
        "----------------\n"
        "Fecha: %s\n"
        "%s\n"
        "Peso actual: %.1f kg\n"
        "Etapa (kgFluido): %.1f kg\n",
        fechaStr, mensaje, kgActual, kgFluido);

    fclose(archivo);
    printf("\nError registrado en archivo: %s\n\n", nombreArchivo);
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

// Lectura de peso desde el puerto serie (float con decimales)
float leerPesoSerial() {
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
            if (intentos > 5000) {
                printf("Timeout leyendo balanza\n");
                fflush(stdout);
                return -1.0;
            }
        }
    }

    char *ptr = strchr(buffer, '+');
    if (!ptr) ptr = strchr(buffer, '-');
    if (!ptr) return 0.0;

    ptr++;
    while (*ptr == ' ') ptr++;

    char numero[16];
    int i = 0;
    while (((*ptr >= '0' && *ptr <= '9') || *ptr == '.') && i < 15) {
        numero[i++] = *ptr;
        ptr++;
    }
    numero[i] = '\0';

    return atof(numero);
}

// Llenado de cada etapa con control de tara
void llenarEtapa(float objetivo, float *kgActual, float *kgFluido, int pinBomba, int pinValvula) {
    digitalWrite(pinBomba, HIGH);
    digitalWrite(pinValvula, HIGH);

    if(pinBomba == BOMBA1)
        printf("Bomba y válvula del agua encendidas (BOMBA1=%d, VALV1=%d)\n\n", pinBomba, pinValvula);
    else
        printf("Bomba y válvula del ASF40 encendidas (BOMBA2=%d, VALV2=%d)\n\n", pinBomba, pinValvula);

    float pesoInicio = leerPesoSerial();
    *kgActual = pesoInicio;
    *kgFluido = 0.0;

    // Chequeo inicial de tara
    if (*kgFluido < 0) {
        apagarTodo();
        const char *msg = "Cuidado al tarar la balanza durante el proceso,\nesto se debe realizar antes de iniciar el programa.";
        printf("\n%s\n", msg);
        guardarError(msg, *kgActual, *kgFluido);
        fflush(stdout);
        close(serialFd);
        exit(1);
    }

    float objetivoEtapa = pesoInicio + objetivo;
    float pesoCambio = pesoInicio;

    while(*kgActual + 0.5 < objetivoEtapa) {
        float peso = leerPesoSerial();
        if(peso >= -100 && peso <= 2000) {
            *kgActual = peso;
            *kgFluido = *kgActual - pesoInicio;

            // Chequeo continuo de tara
            if (*kgFluido < 0) {
                apagarTodo();
                const char *msg = "Cuidado al tarar la balanza durante el proceso,\nesto se debe realizar antes de iniciar el programa.";
                printf("\n%s\n", msg);
                guardarError(msg, *kgActual, *kgFluido);
                fflush(stdout);
                close(serialFd);
                exit(1);
            }

            if(peso != pesoCambio){
                printf("Peso actual: %.1f kg (Etapa: %.1f kg)\n", *kgActual, *kgFluido);
                pesoCambio = peso;
            }
        }
    }

    digitalWrite(pinBomba, LOW);
    digitalWrite(pinValvula, LOW);
    printf("\nEtapa completada. Actual total: %.1f kg\n", *kgActual);
}

// Guardar resultados normales
void guardarResultados(float kgAgua, float kgAFS40, float kgTotal) {
    struct stat st = {0};
    if (stat("mezclas", &st) == -1) {
        mkdir("mezclas", 0755);
    }

    char nombreArchivo[128];
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    strftime(nombreArchivo, sizeof(nombreArchivo), "mezclas/mezcla_%Y%m%d_%H%M%S.txt", tm_info);

    FILE *archivo = fopen(nombreArchivo, "w");
    if(!archivo) return;

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

    printf("\nValor aceptado: %.1f kg\n\n", kgObjetivo);

    float kgActual = 0, kgAgua = 0, kgAFS40 = 0;

    // Etapa 1: Agua
    llenarEtapa(kgObjetivo * PORC_AGUA, &kgActual, &kgAgua, BOMBA1, VALV1);

    // Etapa 2: AFS40
    llenarEtapa(kgObjetivo * PORC_AFS40, &kgActual, &kgAFS40, BOMBA2, VALV2);

    printf("\nProceso terminado.\n");
    printf("Agua: \t\t%.1f kg\n", kgAgua);
    printf("AFS40: \t\t%.1f kg\n", kgAFS40);
    printf("Mezcla total:\t%.1f kg\n", kgAgua+kgAFS40);

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
