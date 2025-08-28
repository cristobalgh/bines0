#include <wiringPi.h>
#include <wiringSerial.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

// ================== CONFIGURACIÓN ==================
typedef struct {
    const char *version;

    int bomba1;
    int bomba2;
    int valv1;
    int valv2;

    int flujo1;
    int flujo2;

    float porcAgua;
    float porcAFS40;

    float kgObjetivoDefault;

    float pesoMin;
    float pesoMax;
    float toleranciaKg;

    const char *serialPuerto;
    int serialBaud;
    int serialSleepUs;
    int serialMaxIntentos;

    const char *dirMezclas;
    const char *fmtArchivoOk;
    const char *fmtArchivoErr;

    const char *msgTodoApagado;
    const char *msgTarado;
    const char *msgFlowStartWarn;
    const char *msgFlowDropWarn;
    const char *msgFlowOppositeErr;
    const char *msgSerialTimeout;
    const char *msgResultadosGuardados;

    int flujoActivoNivel; // 0 = activo bajo, 1 = activo alto
    int ignorarFlujos;    // 0 = chequear, 1 = ignorar completamente
} Config;

static Config cfg = {
    .version = "2.1.0",

    .bomba1 = 2,
    .bomba2 = 5,
    .valv1 = 6,
    .valv2 = 8,

    .flujo1 = 15,
    .flujo2 = 16,

    .porcAgua = 0.375f,
    .porcAFS40 = 0.625f,

    .kgObjetivoDefault = 40.0f,

    .pesoMin = -100.0f,
    .pesoMax = 2000.0f,
    .toleranciaKg = 0.5f,

    .serialPuerto = "/dev/ttyUSB0",
    .serialBaud = B9600,
    .serialSleepUs = 1000,
    .serialMaxIntentos = 5000,

    .dirMezclas = "mezclas",
    .fmtArchivoOk = "mezclas/mezcla_%Y%m%d_%H%M%S.txt",
    .fmtArchivoErr = "mezclas/error_%Y%m%d_%H%M%S.txt",

    .msgTodoApagado = "** TODAS LAS SALIDAS APAGADAS **",
    .msgTarado = "Cuidado al tarar la balanza durante el proceso.",
    .msgFlowStartWarn = "Aviso: el switch de flujo estaba prendido al inicio de la etapa.",
    .msgFlowDropWarn = "Aviso: switch de flujo %d se apagó durante la etapa.",
    .msgFlowOppositeErr = "ERROR: switch de la bomba contraria %d activado durante la etapa.",
    .msgSerialTimeout = "Timeout leyendo balanza.",
    .msgResultadosGuardados = "Resultados guardados en archivo: %s",

    .flujoActivoNivel = 0, // 0 = activo bajo, 1 = activo alto
    .ignorarFlujos = 1
};

// ================== GLOBALES ==================
static int serialFd = -1;

// ================== UTILIDADES ==================
static void asegurarCarpeta(const char *dirname) {
    struct stat st = {0};
    if (stat(dirname, &st) == -1) mkdir(dirname, 0755);
}

static void apagarTodo(void) {
    digitalWrite(cfg.bomba1, LOW);
    digitalWrite(cfg.bomba2, LOW);
    digitalWrite(cfg.valv1, LOW);
    digitalWrite(cfg.valv2, LOW);
    printf("\n%s\n", cfg.msgTodoApagado);
    fflush(stdout);
}

static void manejarCtrlC(int sig) {
    (void)sig;
    apagarTodo();
    if (serialFd > 0) close(serialFd);
    printf("Programa terminado por usuario (Ctrl+C), todas las salidas apagadas\n");
    fflush(stdout);
    exit(0);
}

// ================== ARCHIVOS ==================
static void guardarError(const char *mensaje, float kgActual, float kgFluido) {
    asegurarCarpeta(cfg.dirMezclas);

    char nombreArchivo[256];
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    strftime(nombreArchivo, sizeof(nombreArchivo), cfg.fmtArchivoErr, tm_info);

    FILE *f = fopen(nombreArchivo, "w");
    if (!f) return;

    char fechaStr[64];
    strftime(fechaStr, sizeof(fechaStr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f,
        "ERROR DE PROCESO\n"
        "Fecha: %s\n"
        "%s\n"
        "Peso actual: %.1f kg\n"
        "Etapa (kgFluido): %.1f kg\n",
        fechaStr, mensaje, kgActual, kgFluido);

    fclose(f);
    printf("\nError registrado en archivo: %s\n", nombreArchivo);
    fflush(stdout);
}

static void guardarResultados(float kgAgua, float kgAFS40, float kgTotal) {
    asegurarCarpeta(cfg.dirMezclas);

    char nombreArchivo[256];
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    strftime(nombreArchivo, sizeof(nombreArchivo), cfg.fmtArchivoOk, tm_info);

    FILE *f = fopen(nombreArchivo, "w");
    if (!f) return;

    char fechaStr[64];
    strftime(fechaStr, sizeof(fechaStr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f,
        "Reporte de mezcla\n"
        "Fecha: %s\n"
        "Agua: %.1f kg\n"
        "AFS40: %.1f kg\n"
        "Total: %.1f kg\n",
        fechaStr, kgAgua, kgAFS40, kgTotal);

    fclose(f);
    printf("\033[1;32m");
    printf(cfg.msgResultadosGuardados, nombreArchivo);
    printf("\033[0m\n");
    fflush(stdout);
}

// ================== SERIAL ==================
static int configurarSerial(const char *puerto) {
    int fd = open(puerto, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) { perror("Error abriendo puerto"); return -1; }

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

    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

static float leerPesoSerial(void) {
    static char buffer[512];
    int idx = 0, intentos = 0;

    while (1) {
        if (serialDataAvail(serialFd) > 0) {
            char c = serialGetchar(serialFd);
            if (c == '\r') continue;
            if (c == '\n') { buffer[idx] = '\0'; break; }
            if (idx < (int)sizeof(buffer) - 1) buffer[idx++] = c;
        } else {
            usleep(cfg.serialSleepUs);
            if (++intentos > cfg.serialMaxIntentos) { printf("%s\n", cfg.msgSerialTimeout); return -1.0f; }
        }
    }

    char *ptr = strchr(buffer, '+');
    if (!ptr) ptr = strchr(buffer, '-');
    if (!ptr) return 0.0f;
    ptr++;
    while (*ptr == ' ') ptr++;

    char numero[32];
    int i = 0;
    while (((*ptr >= '0' && *ptr <= '9') || *ptr == '.') && i < (int)sizeof(numero) - 1) numero[i++] = *ptr++;
    numero[i] = '\0';

    return (float)atof(numero);
}

// ================== LLENADO ==================
static void abortarPorTarado(float kgActual, float kgFluido) {
    apagarTodo();
    printf("\n%s\n", cfg.msgTarado);
    guardarError(cfg.msgTarado, kgActual, kgFluido);
    if (serialFd > 0) close(serialFd);
    exit(1);
}

static void llenarEtapa(float objetivo, float *kgActual, float *kgFluido,
                        int pinBomba, int pinValvula,
                        int flujoPin, int flujoContrarioPin) {
    if (!cfg.ignorarFlujos && digitalRead(flujoPin) != cfg.flujoActivoNivel) {
        printf("%s\n", cfg.msgFlowStartWarn);
    }

    digitalWrite(pinBomba, HIGH);
    digitalWrite(pinValvula, HIGH);

    float pesoInicio = leerPesoSerial();
    *kgActual = pesoInicio;
    *kgFluido = 0.0f;

    if (*kgFluido < 0.0f) abortarPorTarado(*kgActual, *kgFluido);

    const float objetivoEtapa = objetivo;
    float pesoCambio = pesoInicio;
    int flujoPrevio = digitalRead(flujoPin);

    while ((*kgFluido + cfg.toleranciaKg) < objetivoEtapa) {
        float peso = leerPesoSerial();
        if (peso >= cfg.pesoMin && peso <= cfg.pesoMax) {
            *kgActual = peso;
            *kgFluido = *kgActual - pesoInicio;
            if (*kgFluido < 0.0f) abortarPorTarado(*kgActual, *kgFluido);

            if (peso != pesoCambio) {
                printf("Peso actual: %.1f kg (Etapa: %.1f kg)\n", *kgActual, *kgFluido);
                pesoCambio = peso;
            }
        }

        if (!cfg.ignorarFlujos) {
            int flujoActual = digitalRead(flujoPin);
            int flujoContrario = digitalRead(flujoContrarioPin);

            if (flujoActual != flujoPrevio) {
                if (flujoActual != cfg.flujoActivoNivel) printf(cfg.msgFlowDropWarn, flujoPin), printf("\n");
                flujoPrevio = flujoActual;
            }

            if (flujoContrario == cfg.flujoActivoNivel) {
                apagarTodo();
                printf("\n");
                printf(cfg.msgFlowOppositeErr, flujoContrarioPin);
                printf("\n");
                if (serialFd > 0) close(serialFd);
                exit(1);
            }
        }
    }

    digitalWrite(pinBomba, LOW);
    digitalWrite(pinValvula, LOW);
    printf("Etapa completada. Actual total: %.1f kg\n", *kgActual);
}

// ================== MAIN ==================
int main(int argc, char *argv[]) {
    signal(SIGINT, manejarCtrlC);

    printf("=== Programa de mezcla de fluidos ===\n");
    printf("Versión: %s\n", cfg.version);

    if (wiringPiSetup() == -1) { fprintf(stderr, "Error iniciando WiringPi\n"); return 1; }

    pinMode(cfg.bomba1, OUTPUT); pinMode(cfg.bomba2, OUTPUT);
    pinMode(cfg.valv1, OUTPUT); pinMode(cfg.valv2, OUTPUT);
    pinMode(cfg.flujo1, INPUT); pinMode(cfg.flujo2, INPUT);
    pullUpDnControl(cfg.flujo1, PUD_DOWN); pullUpDnControl(cfg.flujo2, PUD_DOWN);

    serialFd = configurarSerial(cfg.serialPuerto);
    if (serialFd == -1) return 1;

    // Argumentos
    float kgObjetivo = 10.0f;
    float porcAgua = cfg.porcAgua;

    if (argc == 1) {
       printf("\033[1;33mUso\t\t: sudo %s <kg objetivo> <%% agua>\n", argv[0]);
       printf("<kg objetivo>\t: kilos totales a preparar (ejemplo: 50)\n");
       printf("<%% Agua>\t: fracción de agua entre 0 y 1 (ejemplo: 0.4 para 40%% agua)\n");
       printf("Si no se entregan argumentos, se usan valores por defecto: %.1f kg , %.1f%% agua y %.1f%% afs40.\033[0m\n",
           kgObjetivo, porcAgua * 100, (1-porcAgua)*100);
    }

    if (argc == 2) {
        kgObjetivo = (float)atof(argv[1]);
    } else if (argc == 3) {
        kgObjetivo = (float)atof(argv[1]);
        porcAgua = (float)atof(argv[2]);
        if (porcAgua < 0.0f) porcAgua = 0.0f;
        if (porcAgua > 1.0f) porcAgua = 1.0f;
    }

    float porcAFS40 = 1.0f - porcAgua;

    printf("Objetivo total: %.1f kg\n", kgObjetivo);
    printf("\033[1;31mReparto -> Agua: %.1f%% | AFS40: %.1f%%\033[0m\n", porcAgua * 100, porcAFS40 * 100);


    float kgActual = 0.0f, kgAgua = 0.0f, kgAFS40 = 0.0f;

    if (porcAgua > 0.0f) {
        llenarEtapa(kgObjetivo * porcAgua, &kgActual, &kgAgua, cfg.bomba1, cfg.valv1, cfg.flujo1, cfg.flujo2);
    }

    if (porcAFS40 > 0.0f) {
        llenarEtapa(kgObjetivo * porcAFS40, &kgActual, &kgAFS40, cfg.bomba2, cfg.valv2, cfg.flujo2, cfg.flujo1);
    }

    printf("Proceso terminado.\nAgua: %.1f kg | AFS40: %.1f kg | Total: %.1f kg\n",
           kgAgua, kgAFS40, kgAgua + kgAFS40);

    guardarResultados(kgAgua, kgAFS40, kgActual);

    close(serialFd);
    return 0;
}
