#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <comando>\n", argv[0]);
        exit(1);
    }

    struct timeval start, end;

    // Obtener el tiempo de inicio
    gettimeofday(&start, NULL);

    pid_t pid = fork();
    if (pid < 0) {
        perror("Error al hacer fork");
        exit(1);
    } else if (pid == 0) {
        // Proceso hijo ejecuta el comando
        execvp(argv[1], &argv[1]);
        perror("Error al ejecutar el comando");
        exit(1);
    } else {
        // Proceso padre espera al hijo
        wait(NULL);

        // Obtener el tiempo de fin
        gettimeofday(&end, NULL);

        // Calcular el tiempo transcurrido en segundos y microsegundos
        double elapsed_time = (end.tv_sec - start.tv_sec) + 
                              (end.tv_usec - start.tv_usec) / 1000000.0;
        printf("Elapsed time: %.5f seconds\n", elapsed_time);
    }

    return 0;
}
