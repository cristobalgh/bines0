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

// ================== CONFIGURACIÓN (todo editable aquí) ==================
typedef struct {
    const char *version;

    // Pines bombas y válvulas (WiringPi numbering)
    int bomba1;
    int bomba2;
    int valv1;
    int valv2;

    // Pines switches de flujo (entrada, con pull-down)
    int flujo1;
    int flujo2;

    // Porcentajes de mezcla por defecto (deben sumar 1.0 si no se pasan por CLI/menu)
    float porcAgua;
    float porcAFS40;

    // kg objetivo por defecto (si no se pasan por CLI/menu)
    float kgObjetivoDefault;

    // Límites y tolerancias
    float pesoMin;           // kg mínimos aceptados desde la balanza
    float pesoMax;           // kg máximos aceptados desde la balanza
    float toleranciaKg;      // margen para detener etapa (ej: 0.5 kg)

    // Serial
    const char *serialPuerto;
    int serialBaud;          // ej: B9600
    int serialSleepUs;       // usleep entre lecturas serial
    int serialMaxIntentos;   // intentos para timeout de balanza

    // Archivos / carpetas
    const char *dirMezclas;  // carpeta de salida
    const char *fmtArchivoOk; // patrón archivo resultados
    const char *fmtArchivoErr;// patrón archivo errores

    // Mensajes personalizables
    const char *msgTodoApagado;
    const char *msgTarado;
    const char *msgFlowStartWarn;
    const char *msgFlowDropWarn;      // requiere %d
    const char *msgFlowOppositeErr;   // requiere %d
    const char *msgSerialTimeout;
    const char *msgResultadosGuardados; // requiere %s
} Config;

static Config cfg = {
    .version                = "2.0.0",

    .bomba1                 = 2,
    .bomba2                 = 5,
    .valv1                  = 6,
    .valv2                  = 8,

    .flujo1                 = 15,
    .flujo2                 = 16,

    .porcAgua               = 0.375f,
    .porcAFS40              = 0.625f,

    .kgObjetivoDefault      = 40.0f,

    .pesoMin                = -100.0f,
    .pesoMax                = 2000.0f,
    .toleranciaKg           = 0.5f,

    .serialPuerto           = "/dev/ttyUSB0",
    .serialBaud             = B9600,
    .serialSleepUs          = 1000,
    .serialMaxIntentos      = 5000,

    .dirMezclas             = "mezclas",
    .fmtArchivoOk           = "mezclas/mezcla_%Y%m%d_%H%M%S.txt",
    .fmtArchivoErr          = "mezclas/error_%Y%m%d_%H%M%S.txt",

    .msgTodoApagado         = "** TODAS LAS SALIDAS APAGADAS **",
    .msgTarado              = "Cuidado al tarar la balanza durante el proceso, esto se debe realizar antes de iniciar el programa.",
    .msgFlowStartWarn       = "Aviso: el switch de flujo estaba prendido al inicio de la etapa.",
    .msgFlowDropWarn        = "Aviso: switch de flujo %d se apago y durante la etapa.",
    .msgFlowOppositeErr     = "ERROR: switch de la bomba contraria %d activado durante la etapa.",
    .msgSerialTimeout       = "Timeout leyendo balanza.",
    .msgResultadosGuardados = "Resultados guardados en archivo: %s"
};

// ================== GLOBALES ==================
static int serialFd = -1;

// ================== UTILIDADES I/O CONSOLA ==================
static float leerFloatDefault(const char *mensaje, float valorDefault) {
    char buffer[128];
    printf("%s [%.3f]: ", mensaje, valorDefault);
    fflush(stdout);
    if (fgets(buffer, sizeof(buffer), stdin) == NULL || buffer[0] == '\n') return valorDefault;
    return (float)atof(buffer);
}

static int leerIntDefault(const char *mensaje, int valorDefault) {
    char buffer[128];
    printf("%s [%d]: ", mensaje, valorDefault);
    fflush(stdout);
    if (fgets(buffer, sizeof(buffer), stdin) == NULL || buffer[0] == '\n') return valorDefault;
    return atoi(buffer);
}

// ================== SISTEMA ==================
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
static void asegurarCarpeta(const char *dirname) {
    struct stat st = {0};
    if (stat(dirname, &st) == -1) {
        if (mkdir(dirname, 0755) != 0) {
            fprintf(stderr, "Error creando carpeta '%s': %s\n", dirname, strerror(errno));
        }
    }
}

static void guardarError(const char *mensaje, float kgActual, float kgFluido) {
    asegurarCarpeta(cfg.dirMezclas);

    char nombreArchivo[256];
    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    strftime(nombreArchivo, sizeof(nombreArchivo), cfg.fmtArchivoErr, tm_info);

    FILE *f = fopen(nombreArchivo, "w");
    if (!f) {
        fprintf(stderr, "Error creando archivo de error '%s': %s\n", nombreArchivo, strerror(errno));
        return;
    }

    char fechaStr[64];
    strftime(fechaStr, sizeof(fechaStr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f,
        "ERROR DE PROCESO\n"
        "----------------\n"
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
    if (!f) {
        fprintf(stderr, "Error creando archivo '%s': %s\n", nombreArchivo, strerror(errno));
        return;
    }

    char fechaStr[64];
    strftime(fechaStr, sizeof(fechaStr), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(f,
        "Reporte de mezcla\n"
        "-----------------\n"
        "Fecha: %s\n"
        "Agua (bomba 1): %.1f kg\n"
        "AFS40 (bomba 2): %.1f kg\n"
        "Mezcla total: %.1f kg\n",
        fechaStr, kgAgua, kgAFS40, kgTotal);

    fclose(f);
    printf(cfg.msgResultadosGuardados, nombreArchivo);
    printf("\n");
    fflush(stdout);
}

// ================== SERIAL ==================
static int configurarSerial(const char *puerto) {
    int fd = open(puerto, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("Error abriendo puerto");
        return -1;
    }

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

// Lee una línea del puerto, extrae número con 1 decimal si existe.
// Retorna atof(numero). En timeout retorna -1.0.
static float leerPesoSerial(void) {
    static char buffer[512];
    int idx = 0;
    int intentos = 0;

    while (1) {
        if (serialDataAvail(serialFd) > 0) {
            char c = serialGetchar(serialFd);
            if (c == '\r') continue;
            if (c == '\n') { buffer[idx] = '\0'; break; }
            if (idx < (int)sizeof(buffer) - 1) buffer[idx++] = c;
        } else {
            usleep(cfg.serialSleepUs);
            if (++intentos > cfg.serialMaxIntentos) {
                printf("%s\n", cfg.msgSerialTimeout);
                fflush(stdout);
                return -1.0f;
            }
        }
    }

    char *ptr = strchr(buffer, '+');
    if (!ptr) ptr = strchr(buffer, '-');
    if (!ptr) return 0.0f;

    ptr++; // saltar signo
    while (*ptr == ' ') ptr++;

    // Copiar números (con posible '.')
    char numero[32];
    int i = 0;
    while (((*ptr >= '0' && *ptr <= '9') || *ptr == '.') && i < (int)sizeof(numero) - 1) {
        numero[i++] = *ptr++;
    }
    numero[i] = '\0';

    // Forzar 1 decimal en el parseo si viene con más decimales:
    // atof acepta más, pero mostramos/operamos con 1 decimal en prints.
    return (float)atof(numero);
}

// ================== PROCESO DE LLENADO ==================
static void abortarPorTarado(float kgActual, float kgFluido) {
    apagarTodo();
    printf("\n%s\n", cfg.msgTarado);
    guardarError(cfg.msgTarado, kgActual, kgFluido);
    if (serialFd > 0) close(serialFd);
    exit(1);
}

static void llenarEtapa(
    float objetivo, float *kgActual, float *kgFluido,
    int pinBomba, int pinValvula,
    int flujoPin, int flujoContrarioPin
) {
    if (digitalRead(flujoPin) != 0) {
        printf("%s\n", cfg.msgFlowStartWarn);
    }

    digitalWrite(pinBomba, HIGH);
    digitalWrite(pinValvula, HIGH);

    printf("Esperando que switch de flujo %d se active...\n", flujoPin);
    while (digitalRead(flujoPin) != 1) { usleep(cfg.serialSleepUs); }
    printf("Switch de flujo %d activado.\n", flujoPin);

    if (pinBomba == cfg.bomba1) printf("Bomba y válvula del agua encendidas\n\n");
    else                        printf("Bomba y válvula del AFS40 encendidas\n\n");

    float pesoInicio = leerPesoSerial();
    *kgActual = pesoInicio;
    *kgFluido = 0.0f;

    if (*kgFluido < 0.0f) {
        abortarPorTarado(*kgActual, *kgFluido);
    }

    const float objetivoEtapa = pesoInicio + objetivo;
    float pesoCambio = pesoInicio;
    int flujoPrevio = 1;

    while ((*kgActual + cfg.toleranciaKg) < objetivoEtapa) {
        float peso = leerPesoSerial();
        if (peso >= cfg.pesoMin && peso <= cfg.pesoMax) {
            *kgActual = peso;
            *kgFluido = *kgActual - pesoInicio;

            if (*kgFluido < 0.0f) {
                abortarPorTarado(*kgActual, *kgFluido);
            }

            if (peso != pesoCambio) {
                printf("Peso actual: %.1f kg (Etapa: %.1f kg)\n", *kgActual, *kgFluido);
                pesoCambio = peso;
            }
        }

        int flujoActual    = digitalRead(flujoPin);
        int flujoContrario = digitalRead(flujoContrarioPin);

        if (flujoActual != flujoPrevio) {
            if (flujoActual == 0) printf(cfg.msgFlowDropWarn, flujoPin), printf("\n");
            flujoPrevio = flujoActual;
        }

        if (flujoContrario == 1) {
            apagarTodo();
            printf("\n");
            printf(cfg.msgFlowOppositeErr, flujoContrarioPin);
            printf("\n");
            if (serialFd > 0) close(serialFd);
            exit(1);
        }
    }

    digitalWrite(pinBomba, LOW);
    digitalWrite(pinValvula, LOW);
    printf("\nEtapa completada. Actual total: %.1f kg\n", *kgActual);
}

// ================== MENÚ INTERACTIVO ==================
static void configurarPorConsola(void) {
    printf("=== Configuración inicial (Enter = mantener valor) ===\n");

    // Kg objetivo por defecto para esta corrida
    cfg.kgObjetivoDefault = leerFloatDefault("Kg objetivo", cfg.kgObjetivoDefault);

    // Porcentajes (deben sumar 1.0). No normalizamos automáticamente (según pedido).
    while (1) {
        cfg.porcAgua  = leerFloatDefault("Porcentaje Agua (0-1)",  cfg.porcAgua);
        cfg.porcAFS40 = leerFloatDefault("Porcentaje AFS40 (0-1)", cfg.porcAFS40);
        if (cfg.porcAgua > 0.0f && cfg.porcAFS40 > 0.0f) {
            float suma = cfg.porcAgua + cfg.porcAFS40;
            if (suma > 0.999f && suma < 1.001f) break; // tolerancia de igualdad
        }
        printf("Error: los porcentajes deben ser > 0 y sumar 1.0. Intenta nuevamente.\n");
    }

    // Pines bombas / válvulas
    cfg.bomba1 = leerIntDefault("Pin bomba 1 (WiringPi)", cfg.bomba1);
    cfg.valv1  = leerIntDefault("Pin válvula 1 (WiringPi)", cfg.valv1);
    cfg.bomba2 = leerIntDefault("Pin bomba 2 (WiringPi)", cfg.bomba2);
    cfg.valv2  = leerIntDefault("Pin válvula 2 (WiringPi)", cfg.valv2);

    // Pines switches flujo
    cfg.flujo1 = leerIntDefault("Pin switch flujo 1 (WiringPi)", cfg.flujo1);
   cfg.flujo2 = leerIntDefault("Pin switch flujo 2 (WiringPi)", cfg.flujo2);

    // Tolerancias
    cfg.toleranciaKg = leerFloatDefault("Tolerancia etapa (kg)", cfg.toleranciaKg);

    printf("\n— Resumen configuración —\n");
    printf("Kg objetivo: %.1f\n", cfg.kgObjetivoDefault);
    printf("Porcentajes -> Agua: %.3f | AFS40: %.3f\n", cfg.porcAgua, cfg.porcAFS40);
    printf("Bomba1/Valv1: %d/%d | Bomba2/Valv2: %d/%d\n", cfg.bomba1, cfg.valv1, cfg.bomba2, cfg.valv2);
    printf("Flujo1/Flujo2: %d/%d\n", cfg.flujo1, cfg.flujo2);
    printf("Tolerancia kg: %.3f\n\n", cfg.toleranciaKg);
}

// ================== MAIN ==================
int main(int argc, char *argv[]) {
    signal(SIGINT, manejarCtrlC);

    printf("\n=== Programa de mezcla de fluidos ===\n");
    printf("Versión: %s\n\n", cfg.version);

    // Menú interactivo (puedes comentar esta línea si prefieres usar solo argumentos)
//    configurarPorConsola();

    if (wiringPiSetup() == -1) {
        fprintf(stderr, "Error iniciando WiringPi\n");
        return 1;
    }

    // Configurar pines
    pinMode(cfg.bomba1, OUTPUT);
    pinMode(cfg.bomba2, OUTPUT);
    pinMode(cfg.valv1,  OUTPUT);
    pinMode(cfg.valv2,  OUTPUT);

    pinMode(cfg.flujo1, INPUT);
    pinMode(cfg.flujo2, INPUT);
    pullUpDnControl(cfg.flujo1, PUD_DOWN);
    pullUpDnControl(cfg.flujo2, PUD_DOWN);

    // Serial
    serialFd = configurarSerial(cfg.serialPuerto);
    if (serialFd == -1) return 1;

    // Admite argumentos también (opcionales)
    // argv[1]: kg objetivo, argv[2]: porcAgua, argv[3]: porcAFS40
    float kgObjetivo = cfg.kgObjetivoDefault;
    if (argc >= 2) kgObjetivo = (float)atof(argv[1]);
    if (kgObjetivo <= 0.0f || kgObjetivo > 1500.0f) {
        fprintf(stderr, "Error: kg objetivo debe estar entre 0 y 1500.\n");
        return 1;
    }
    if (argc >= 4) {
        float pAgua  = (float)atof(argv[2]);
        float pAFS40 = (float)atof(argv[3]);
        float suma = pAgua + pAFS40;
        if (pAgua > 0.0f && pAFS40 > 0.0f && suma > 0.999f && suma < 1.001f) {
            cfg.porcAgua  = pAgua;
            cfg.porcAFS40 = pAFS40;
        } else {
            fprintf(stderr, "Error: porcentajes inválidos por argumentos. Deben ser >0 y sumar 1.0\n");
            return 1;
        }
    }

    printf("Objetivo total: %.1f kg\n", kgObjetivo);
    printf("Reparto -> Agua: %.3f | AFS40: %.3f\n\n", cfg.porcAgua, cfg.porcAFS40);

    float kgActual = 0.0f, kgAgua = 0.0f, kgAFS40 = 0.0f;

    // Etapa Agua
    llenarEtapa(kgObjetivo * cfg.porcAgua,  &kgActual, &kgAgua,
                cfg.bomba1, cfg.valv1, cfg.flujo1, cfg.flujo2);

    // Etapa AFS40
    llenarEtapa(kgObjetivo * cfg.porcAFS40, &kgActual, &kgAFS40,
                cfg.bomba2, cfg.valv2, cfg.flujo2, cfg.flujo1);

    printf("\nProceso terminado.\n");
    printf("Agua: \t\t%.1f kg\n", kgAgua);
    printf("AFS40: \t\t%.1f kg\n", kgAFS40);
    printf("Mezcla total:\t%.1f kg\n", kgAgua + kgAFS40);

    guardarResultados(kgAgua, kgAFS40, kgActual);

    // Mensaje final tipo "firma"
    printf("\ncgh 8.25 ");
    srand((unsigned)time(NULL));
    for (int i = 0; i < 4; i++) {
        int t = rand() % 3;
        char c = (t == 0) ? ('0' + rand()%10) : (t == 1) ? ('A' + rand()%26) : ('a' + rand()%26);
        putchar(c);
    }
    printf("\n\n");

    close(serialFd);
    return 0;
}
