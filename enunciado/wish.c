#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

// ------------------------------------------- UTILS ------------------------------------------- //

// Inicializa un arreglo dinamico con una capacidad inicial de 2
#define INIT_ARR(arrStruct, arrField, arrType) \
    do { \
        arrStruct.size = 0; \
        arrStruct.capacity = 2; \
        arrStruct.arrField = calloc(arrStruct.capacity, sizeof(arrType)); \
    } while(0)

// Asigna mas espacio para un arreglo dinamico duplicando su capacidad
#define REALLOC(arrStruct, arrField, arrType) \
    if (arrStruct.size == arrStruct.capacity) { \
        arrStruct.capacity *= 2; \
        arrStruct.arrField = realloc(arrStruct.arrField, arrStruct.capacity * sizeof(arrType)); \
    }

// ------------------------------------------- Utilidades ------------------------------------------- //

// -------------------------- Colores y estilos de texto en la terminal -------------------------- //
#define RED "\x1B[31m"
#define GREEN "\x1B[32m"
#define YELLOW "\x1B[33m"
#define BLUE "\x1B[34m"
#define MAGENTA "\x1B[35m"
#define CYAN "\x1B[36m"
#define WHITE "\x1B[37m"
#define RESET "\x1B[0m"
#define BOLD "\x1B[1m"
#define ITALIC "\x1B[3m"
#define UNDERLINE "\x1B[4m"
// -------------------------- COLORS -------------------------- //

// -------------------------- Variables globales -------------------------- //

// Estructura para un arreglo dinamico para almacenar entradas de rutas de comandos
struct {
    char **entries;
    int size;
    int capacity;
} path;

// Estructura para los argumentos de un comando
struct Command {
    char **args;
    int size;
    int capacity;
};

// Estructura de arreglo dinamico para almacenar comandos individuales que se ejecutan en paralelo
struct {
    struct Command **singleCommands;
    int size;
    int capacity;
} commands;

// Linea para leer desde el archivo de entrada
char *inputLine;

// Ruta del archivo a ejecutar
char *filePath;

// Codigo de salida del programa, puede ser 0 (exito) o 1 (error)
int exitCode;

// Indica el modo de procesamiento por lotes (1) o el modo interactivo (0)
int batchProcessing;

// -------------------------- VARIABLES GLOBALES -------------------------- //

/**
 * @brief Comprueba si se puede ejecutar un archivo o comando relativo o absoluto
 * 
 * @param filename El archivo o comando relativo o absoluto a evaluar
 * @param filePath Devuelve la ruta absoluta completa del comando que se pasara a execv
 * @return 1 si es ejcutable, 0 si not
 */
int checkAccess(char *filename, char **filePath) {
    // Verificar si se puede ejecutar la ruta absoluta o relativa
    if (access(filename, X_OK) == 0) {
        strcpy(*filePath, filename);
        return 1;
    }

    // Comprueba si el comando existe en alguna de las rutas
    for (int i = 0; i < path.size; i++) {
        snprintf(*filePath, LINE_MAX, "%s/%s", path.entries[i], filename);
        if (access(*filePath, X_OK) == 0) {
            return 1;
        }
    }
    return 0;
}


/**
 * @brief procesa y ejecuta un comando externo si es valido
 *
 * @param command comando a ejecutar
 * @return 1 si el shell puede continuar, 0 si el shell necesita salir
 */
int processExternalCommand(struct Command *command) {
    int redirectIndex = -1;

    // Busca el simbolo de redireccion en los argumentos
    for (int i = 0; i < command->size; i++) {
        if (strcmp(command->args[i], ">") == 0) {
            redirectIndex = i;

            // Error si el simbolo de redireccion esta al inicio o hay multiples argumentos despues de el
            if (redirectIndex == 0 || command->size - (redirectIndex + 1) != 1) {
                fprintf(stderr, "An error has occurred\n");
                return 1;
            }

            command->args[redirectIndex] = NULL; // Necesario para execv
            break;
        }
    }

    // Verifica si el comando puede ejecutarse
    if (checkAccess(command->args[0], &filePath)) {
        pid_t child = fork();

        // Manejo de error en la creación del proceso hijo
        if (child == -1) {
            fprintf(stderr, "error: %s\n", strerror(errno));
            exitCode = 1;
            return 0;
        }

        // En el proceso hijo
        if (child == 0) {
            if (redirectIndex != -1) {
                int redirectFd = open(command->args[redirectIndex + 1], O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
                if (redirectFd == -1) {
                    fprintf(stderr, "error: %s\n", strerror(errno));
                    exit(1);
                }
                dup2(redirectFd, STDOUT_FILENO);
                dup2(redirectFd, STDERR_FILENO);
                close(redirectFd);
            }
            execv(filePath, command->args);
            // Si execv falla
            fprintf(stderr, "error: %s\n", strerror(errno));
            exit(1);
        }
    } else {
        // Mensajes de error especificos para modo batch o interactivo
        fprintf(stderr, batchProcessing ? "An error has occurred\n" : "%serror:\n\tcommand '%s' not found\n", RED, command->args[0]);
    }

    return 1;
}

/**
 * @brief Procesa y ejecuta un comando si es válida
 *
 * @param command Comando a ejecutar
 * @return 1 si el shell puede continuar, 0 si el shell necesita salir
 */
int processCommand(struct Command *command) {
    // Comando `exit`
    if (strcmp(command->args[0], "exit") == 0) {
        if (command->size > 1) {
            fprintf(stderr, "An error has occurred\n");
            return 1;
        }
        if (!batchProcessing) {
            printf("%sbyeee\n%s", CYAN, RESET);
        }
        return 0;
    }

    // Comando `cd`
    if (strcmp(command->args[0], "cd") == 0) {
        if (command->size != 2) {
            fprintf(stderr, "An error has occurred\n");
            return 1;
        }
        if (chdir(command->args[1]) == -1) {
            fprintf(stderr, "error:\n\tcannot execute command 'cd': %s\n", strerror(errno));
            return 1;
        }
    }

    // Comando `path`
    else if (strcmp(command->args[0], "path") == 0) {
        // Limpia la lista de paths existentes
        for (int i = 0; i < path.size; i++) {
            free(path.entries[i]);
        }
        path.size = 0;

        // Copia los nuevos paths del comando a `path`
        for (int i = 1; i < command->size; i++) {
            path.entries[path.size] = malloc(strlen(command->args[i]) * sizeof(char));
            strcpy(path.entries[path.size++], command->args[i]);
        }
    }

    // Comando `cls`
    else if (strcmp(command->args[0], "cls") == 0) {
        pid_t child = fork();
        if (child == 0) {
            execv("/usr/bin/clear", command->args);
            exit(1); // Termina el proceso hijo si `execv` falla
        } else {
            waitpid(child, NULL, 0);
        }
    }

    // Otros comandos externos
    else {
        return processExternalCommand(command);
    }

    return 1; // Comando procesado correctamente
}

/**
 * @brief Ejecuta el shell obteniendo comandos del archivo proporcionado.
 *
 * @param inputFile Archivo desde el cual se extraen los comandos; puede ser stdin o un archivo del sistema.
 */
void runShell(FILE *inputFile) {
    while (1) {
        // Modo interactivo
        if (!batchProcessing) {
            char *cwd = getcwd(NULL, 0);
            printf("%s%s%s\n", CYAN, cwd, RESET);
            printf("%swish> %s", MAGENTA, RESET);
            free(cwd);
        }

        // Lee la siguiente línea de comando
        if (fgets(inputLine, LINE_MAX, inputFile) == NULL || feof(inputFile)) {
            if (batchProcessing) {
                break;
            }
            fprintf(stderr, "error: %s", strerror(errno));
            exitCode = 1;
            break;
        }

        char *singleCommandSavePtr = NULL;
        commands.size = 0; // Reinicia el tamaño de comandos

        char *singleCommandLine = strtok_r(inputLine, "&", &singleCommandSavePtr);

        while (singleCommandLine != NULL) {
            REALLOC(commands, singleCommands, struct Command *);
            struct Command *singleCommand = commands.singleCommands[commands.size++];

            if (singleCommand == NULL) {
                singleCommand = malloc(sizeof(struct Command));
                INIT_ARR((*singleCommand), args, char *);
                commands.singleCommands[commands.size - 1] = singleCommand;
            }

            singleCommand->size = 0;
            char *tokenSavePtr = NULL;
            char *token = strtok_r(singleCommandLine, " \n\t", &tokenSavePtr);

            while (token != NULL) {
                singleCommand->args[singleCommand->size++] = token;
                REALLOC((*singleCommand), args, char *);
                token = strtok_r(NULL, " \n\t", &tokenSavePtr);
            }

            singleCommand->args[singleCommand->size] = NULL;
            singleCommandLine = strtok_r(NULL, "&", &singleCommandSavePtr);
        }

        if (commands.size > 0 && commands.singleCommands[commands.size - 1]->size == 0) {
            commands.size--;
        }

        int exitShell = 0;
        for (int i = 0; i < commands.size; i++) {
            struct Command *singleCommand = commands.singleCommands[i];
            if (!processCommand(singleCommand)) {
                exitShell = 1;
                break;
            }
        }

        if (exitShell) {
            break;
        }

        for (int i = 0; i < commands.size; i++) {
            wait(NULL);
        }

        if (!batchProcessing) {
            printf("\n");
        }
    }
}

int main(int argc, char const *argv[]) {
    inputLine = malloc(LINE_MAX * sizeof(char));
    filePath = malloc(LINE_MAX * sizeof(char));

    INIT_ARR(commands, singleCommands, struct Command *);
    INIT_ARR(path, entries, char *);

    // Inicializa el path con "/bin"
    path.entries[path.size] = malloc(strlen("/bin") + 1);
    strcpy(path.entries[path.size++], "/bin");

    exitCode = 0;

    // Verifica los argumentos de entrada
    batchProcessing = (argc == 2);
    if (argc > 2) {
        fprintf(stderr, "An error has occurred\n");
        exitCode = 1;
        goto cleanup;
    }

    // Modo batch o interactivo
    FILE *inputFile = (batchProcessing) ? fopen(argv[1], "r") : stdin;
    if (batchProcessing && inputFile == NULL) {
        fprintf(stderr, "An error has occurred\n");
        exitCode = 1;
    } else {
        runShell(inputFile);
        if (batchProcessing) fclose(inputFile);
    }

cleanup:
    // Libera todos los path entries
    for (int i = 0; i < path.size; i++) {
        free(path.entries[i]);
    }
    free(path.entries);

    // Libera todos los comandos individuales
    for (int i = 0; i < commands.capacity; i++) {
        if (commands.singleCommands[i] != NULL) {
            free(commands.singleCommands[i]->args);
            free(commands.singleCommands[i]);
        }
    }
    free(commands.singleCommands);

    // Libera la memoria asignada para `inputLine` y `filePath`
    free(inputLine);
    free(filePath);

    exit(exitCode);
}
