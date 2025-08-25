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

// ================== CONFIGURACIÓN ==================
typedef struct {
    const char *version;

    // Pines bombas y válvulas
    int bomba1;
    int bomba2;
    int valv1;
    int valv2;

    // Pines switches de flujo
    int flujo1;
    int flujo2;

    // Porcentajes de mezcla por defecto
    float porcAgua;
    float porcAFS40;

    // kg objetivo por defecto
    float kgObjetivoDefault;

    // Límites de peso
    float pesoMin;
    float pesoMax;

    // Serial
    const char *serialPuerto;
    int serialBaud;

    // Tiempo de espera serial (microsegundos)
    int serialSleep;
} Config;

Config cfg = {
    .version           = "1.7.0",
    .bomba1            = 2,
    .bomba2            = 5,
    .valv1             = 6,
    .valv2             = 8,
    .flujo1            = 15,
    .flujo2            = 16,
    .porcAgua          = 0.375,
    .porcAFS40         = 0.625,
    .kgObjetivoDefault  = 40.0,
    .pesoMin           = -100.0,
    .pesoMax           = 2000.0,
    .serialPuerto      = "/dev/ttyUSB0",
    .serialBaud        = B9600,
    .serialSleep       = 1000
};

// ================== VARIABLES GLOBALES ==================
int serialFd;

// ================== FUNCIONES ==================
void apagarTodo() {
    digitalWrite(cfg.bomba1, LOW);
    digitalWrite(cfg.bomba2, LOW);
    digitalWrite(cfg.valv1, LOW);
    digitalWrite(cfg.valv2, LOW);
    printf("\n** TODAS LAS SALIDAS APAGADAS **\n");
    fflush(stdout);
}

void manejarCtrlC(int sig) {
    (void)sig;
    apagarTodo();
    if(serialFd > 0) close(serialFd);
    printf("Programa terminado por usuario (Ctrl+C), todas las salidas apagadas\n");
    fflush(stdout);
    exit(0);
}

void guardarError(const char *mensaje, float kgActual, float kgFluido) {
    struct stat st = {0};
    if (stat("mezclas", &st) == -1) mkdir("mezclas", 0755);

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
    printf("\nError registrado en archivo: %s\n", nombreArchivo);
    fflush(stdout);
}

int configurarSerial(const char *puerto) {
    int fd = open(puerto, O_RDWR | O_NOCTTY | O_NDELAY);
    if(fd < 0) { perror("Error abriendo puerto"); return -1; }

    struct termios tty;
    tcgetattr(fd, &tty);

    cfsetispeed(&tty, cfg.serialBaud);
    cfsetospeed(&tty, cfg.serialBaud);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

float leerPesoSerial() {
    static char buffer[512];
    int idx = 0;
    char c;
    int intentos = 0;

    while(1) {
        if(serialDataAvail(serialFd) > 0) {
            c = serialGetchar(serialFd);
            if(c == '\r') continue;
            if(c == '\n') { buffer[idx] = '\0'; break; }
            if(idx < 511) buffer[idx++] = c;
        } else {
            usleep(cfg.serialSleep);
            intentos++;
            if(intentos > 5000) {
                printf("Timeout leyendo balanza\n");
                fflush(stdout);
                return -1.0;
            }
        }
    }

    char *ptr = strchr(buffer, '+');
    if(!ptr) ptr = strchr(buffer, '-');
    if(!ptr) return 0.0;

    ptr++;
    while(*ptr == ' ') ptr++;

    char numero[16];
    int i=0;
    while(((*ptr>='0' && *ptr<='9') || *ptr=='.') && i<15) {
        numero[i++] = *ptr;
        ptr++;
    }
    numero[i]='\0';

    return atof(numero);
}

void guardarResultados(float kgAgua, float kgAFS40, float kgTotal) {
    struct stat st = {0};
    if (stat("mezclas", &st) == -1) mkdir("mezclas", 0755);

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
    fflush(stdout);
}

// ================== FUNCIÓN LLENADO ==================
void llenarEtapa(
    float objetivo, float *kgActual, float *kgFluido,
    int pinBomba, int pinValvula,
    int flujoPin, int flujoContrarioPin
) {
    if(digitalRead(flujoPin) != 0)
        printf("Aviso: switch de flujo %d no estaba en 0 al inicio de la etapa.\n", flujoPin);

    digitalWrite(pinBomba, HIGH);
    digitalWrite(pinValvula, HIGH);

    printf("Esperando que switch de flujo %d se active...\n", flujoPin);
    while(digitalRead(flujoPin)!=1) { usleep(cfg.serialSleep); }
    printf("Switch de flujo %d activado.\n", flujoPin);

    if(pinBomba==cfg.bomba1) printf("Bomba y válvula del agua encendidas\n\n");
    else printf("Bomba y válvula del ASF40 encendidas\n\n");

    float pesoInicio = leerPesoSerial();
    *kgActual = pesoInicio;
    *kgFluido = 0.0;

    if(*kgFluido < 0){
        apagarTodo();
        const char *msg = "Cuidado al tarar la balanza durante el proceso, esto se debe realizar antes de iniciar el programa.";
        printf("\n%s\n", msg);
        guardarError(msg, *kgActual, *kgFluido);
        close(serialFd);
        exit(1);
    }

    float objetivoEtapa = pesoInicio + objetivo;
    float pesoCambio = pesoInicio;
    int flujoPrevio = 1;

    while(*kgActual + 0.5 < objetivoEtapa){
        float peso = leerPesoSerial();
        if(peso >= cfg.pesoMin && peso <= cfg.pesoMax){
            *kgActual = peso;
            *kgFluido = *kgActual - pesoInicio;

            if(*kgFluido < 0){
                apagarTodo();
                const char *msg = "Cuidado al tarar la balanza durante el proceso, esto se debe realizar antes de iniciar el programa.";
                printf("\n%s\n", msg);
                guardarError(msg, *kgActual, *kgFluido);
                close(serialFd);
                exit(1);
            }

            if(peso != pesoCambio){
                printf("Peso actual: %.1f kg (Etapa: %.1f kg)\n", *kgActual, *kgFluido);
                pesoCambio = peso;
            }
        }

        int flujoActual = digitalRead(flujoPin);
        int flujoContrario = digitalRead(flujoContrarioPin);

        if(flujoActual != flujoPrevio){
            if(flujoActual == 0)
                printf("Aviso: switch de flujo %d pasó a 0 durante la etapa.\n", flujoPin);
            flujoPrevio = flujoActual;
        }

        if(flujoContrario == 1){
            apagarTodo();
            printf("\nERROR: switch de la bomba contraria %d activado durante la etapa.\n", flujoContrarioPin);
            close(serialFd);
            exit(1);
        }
    }

    digitalWrite(pinBomba, LOW);
    digitalWrite(pinValvula, LOW);
    printf("\nEtapa completada. Actual total: %.1f kg\n", *kgActual);
}

// ================== MAIN ==================
int main(int argc, char *argv[]){
    signal(SIGINT, manejarCtrlC);

    printf("\n=== Programa de mezcla de fluidos ===\n");
    printf("Versión: %s\n", cfg.version);

    if(wiringPiSetup() == -1){
        printf("Error iniciando WiringPi\n"); 
        return 1;
    }

    // Pines bombas y válvulas
    pinMode(cfg.bomba1, OUTPUT);
    pinMode(cfg.bomba2, OUTPUT);
    pinMode(cfg.valv1, OUTPUT);
    pinMode(cfg.valv2, OUTPUT);

    // Pines switches de flujo con pull-down
    pinMode(cfg.flujo1, INPUT);
    pinMode(cfg.flujo2, INPUT);
    pullUpDnControl(cfg.flujo1, PUD_DOWN);
    pullUpDnControl(cfg.flujo2, PUD_DOWN);

    serialFd = configurarSerial(cfg.serialPuerto);
    if(serialFd == -1) return 1;

    // kg objetivo
    float kgObjetivo = cfg.kgObjetivoDefault; // valor por defecto
    if(argc >= 2) kgObjetivo = atof(argv[1]);

    // porcentajes opcionales
    if(argc >= 4){
        float pAgua = atof(argv[2]);
        float pAFS40 = atof(argv[3]);
        if(pAgua <=0 || pAFS40<=0 || (pAgua+pAFS40)!=1.0){
            printf("Error: porcentajes inválidos. Deben ser >0 y sumar 1.0\n");
            return 1;
        }
        cfg.porcAgua = pAgua;
        cfg.porcAFS40 = pAFS40;
    }

    printf("\nValor aceptado: %.1f kg\n", kgObjetivo);
    printf("Porcentaje Agua: %.3f, Porcentaje AFS40: %.3f\n\n", cfg.porcAgua, cfg.porcAFS40);

    float kgActual = 0, kgAgua = 0, kgAFS40 = 0;

    llenarEtapa(kgObjetivo*cfg.porcAgua, &kgActual, &kgAgua, cfg.bomba1, cfg.valv1, cfg.flujo1, cfg.flujo2);
    llenarEtapa(kgObjetivo*cfg.porcAFS40, &kgActual, &kgAFS40, cfg.bomba2, cfg.valv2, cfg.flujo2, cfg.flujo1);

    printf("\nProceso terminado.\n");
    printf("Agua: \t\t%.1f kg\n", kgAgua);
    printf("AFS40: \t\t%.1f kg\n", kgAFS40);
    printf("Mezcla total:\t%.1f kg\n", kgAgua + kgAFS40);

    guardarResultados(kgAgua, kgAFS40, kgActual);

    printf("\ncgh 8.25 ");
    srand(time(NULL));
    for(int i=0; i<4; i++){
        char c;
        int tipo = rand() % 3;
        if(tipo==0) c = '0'+rand()%10;
        else if(tipo==1) c = 'A'+rand()%26;
        else c = 'a'+rand()%26;
        printf("%c", c);
    }
    printf("\n\n");

    close(serialFd);
    return 0;
}
