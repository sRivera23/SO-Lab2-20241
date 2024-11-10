#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <bits/posix2_lim.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

// ------------------------------------------- UTILS ------------------------------------------- //
// initialize a dynamic array with initial capacity at 2
#define INIT_ARR(arrStruct, arrField, arrType) \
    arrStruct.size = 0;                        \
    arrStruct.capacity = 2;                    \
    arrStruct.arrField = calloc(sizeof(arrType), arrStruct.capacity);

// get more space for a dynamic array by doubling its capacity
#define REALLOC(arrStruct, arrField, arrType)                                                   \
    if (arrStruct.size == arrStruct.capacity)                                                   \
    {                                                                                           \
        arrStruct.capacity *= 2;                                                                \
        arrStruct.arrField = realloc(arrStruct.arrField, sizeof(arrType) * arrStruct.capacity); \
    }
// ------------------------------------------- UTILS ------------------------------------------- //

// -------------------------- COLORS -------------------------- //
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

// -------------------------- GLOBAL VARIABLES -------------------------- //
// dynamic array for path
struct
{
    char **entries;
    int size;
    int capacity;
} path;

// dynamic array for a command's arguments
struct Command
{
    char **args;
    int size;
    int capacity;
};

// dynamic array for single commands executing in parallel
struct
{
    struct Command **singleCommands;
    int size;
    int capacity;
} commands;

// line to read from input file
char *inputLine;

// the file's path that'll be executed
char *filePath;

// program's exit code, can be 0 (success) or 1 (error);
int exitCode;

// when 1 indicates that the shell is processing a batch file, when 0 is in interactive mode
int batchProcessing;
// -------------------------- GLOBAL VARIABLES -------------------------- //

/**
 * @brief Checks if a relative or absolute file or command can be executed
 *
 * @param filename the relative or absolute file or command to evaluate
 * @param filePath returns the complete absolute path of the command to pass to execv
 * @return 1 if true, 0 if false
 */
int checkAccess(char *filename, char **filePath)
{
    int canAccess;

    // first check if absolute or relative path can be executed
    canAccess = 1 + access(filename, X_OK);
    if (canAccess)
    {
        strcpy(*filePath, filename);
        return canAccess;
    }

    for (int i = 0; i < path.size; i++)
    {
        char cat[strlen(path.entries[i]) + strlen(filename) + 1];
        strcpy(cat, path.entries[i]);
        strcat(cat, "/");
        strcat(cat, filename);
        canAccess = 1 + access(cat, X_OK);
        if (canAccess)
        {
            strcpy(*filePath, cat);
            return canAccess;
        }
    }
    return 0;
}

/**
 * @brief processes and executes an external command if valid
 *
 * @param command Command to execute
 * @return 1 if shell can continue, 0 if shell needs to exit
 */
int processExternalCommand(struct Command *command)
{
    // Busca el símbolo de redirección en los argumentos del comando
    int redirectIndex = -1;
    for (int i = 0; i < command->size; i++)
    {

        // Si se encuentra el símbolo de redirección
        if (strcmp(command->args[i], ">") == 0)
        {
            redirectIndex = i;
            if (redirectIndex == 0)
            {
                fprintf(stderr, "An error has occurred\n");
                return 1;
            }

            // Verifica si hay un nombre de archivo de salida después del símbolo de redirección
            if (command->size - (redirectIndex + 1) != 1)
            {
                fprintf(stderr, "An error has occurred\n");
                return 1;
            }

            command->args[redirectIndex] = NULL; // to call execv (see docs)
            break;
        }
    }

    // check if command can be executed in any path entry
    if (checkAccess(command->args[0], &filePath))
    {

        // create child process
        pid_t child = fork();
        if (child == -1) // error
        {
            fprintf(stderr, "error: %s\n", strerror(errno));
            exitCode = 1;
            return 0;
        }
        if (child == 0) // in child's process
        {
            if (redirectIndex != -1)
            {
                int redirectFileDescriptor = open(command->args[redirectIndex + 1], O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0666);
                dup2(redirectFileDescriptor, 1); // stdout
                dup2(redirectFileDescriptor, 2); // stderr
                close(redirectFileDescriptor);
            }
            execv(filePath, command->args);
        }
        // else: wait outside for all children (all single commands)
    }
    else
    {
        if (batchProcessing)
            fprintf(stderr, "An error has occurred\n");
        else
            fprintf(stderr, "%serror:\n\tcommand '%s' not found\n", RED, command->args[0]);
    }
    return 1;
}

/**
 * @brief processes and executes a command if valid
 *
 * @param command Command to execute
 * @return 1 if shell can continue, 0 if shell needs to exit
 */
int processCommand(struct Command *command)
{

    // ----------------------------------------------------------------------------------- //
    // -------------------------------- BUILT-IN COMMANDS -------------------------------- //
    if (strcmp(command->args[0], "exit") == 0)
    {
        if (command->size > 1)
        {
            fprintf(stderr, "An error has occurred\n");
            return 1;
        }
        if (!batchProcessing)
            printf("%sbyeee\n%s", CYAN, RESET);
        return 0;
    }

    else if (strcmp(command->args[0], "cd") == 0)
    {
        if (command->size != 2)
        {
            fprintf(stderr, "An error has occurred\n");
            return 1;
        }
        if (chdir(command->args[1]) == -1)
        {
            fprintf(stderr, "error:\n\tcannot execute command 'cd': %s\n", strerror(errno));
            return 1;
        }
    }

    else if (strcmp(command->args[0], "path") == 0)
    {
        // delete all entries in path
        for (int i = 0; i < path.size; i++)
        {
            free(path.entries[i]);
        }
        path.size = 0;

        // copy all paths from command to path
        for (int i = 1; i < command->size; i++)
        {
            path.entries[path.size] = malloc(strlen(command->args[i]) * sizeof(char));
            strcpy(path.entries[path.size++], command->args[i]);
        }
    }
    else if (strcmp(command->args[0], "cls") == 0)
    {
        pid_t child = fork();
        if (child == 0)
            execv("/usr/bin/clear", command->args);
        else
            waitpid(child, NULL, 0);
    }
    // -------------------------------- BUILT-IN COMMANDS -------------------------------- //
    // ----------------------------------------------------------------------------------- //
    else
    {
        return processExternalCommand(command);
    }

    return 1; // Indica que el comando se procesó correctamente
}

/**
 * @brief Runs the shell getting input commands from the provided file
 *
 * @param inputFile The file where each command is extracted, can be stdin or a custom system file
 */
void runShell(FILE *inputFile)
{
    while (1)
    {
        // if in interactive mode
        if (!batchProcessing)
        {
            char *cwd = getcwd(NULL, 0);
            printf("%s%s%s\n", CYAN, cwd, RESET);
            printf("%swish> %s", MAGENTA, RESET);
            free(cwd);
        }

        // get next line or command from input file
        fgets(inputLine, LINE_MAX, inputFile);

        // if some error or end of file
        if (inputLine == NULL || feof(inputFile))
        {
            if (batchProcessing)
            {
                break;
            }
            fprintf(stderr, "error: %s", strerror(errno));
            exitCode = 1;
            break;
        }

        // used to track single commands as tokens
        char *singleCommandSavePtr = NULL;

        // used to track command's tokens
        char *tokenSavePtr = NULL;

        struct Command *singleCommand;

        // to extract single commands from inputLine
        char *singleCommandLine;
        
        // to extract tokens from singleCommandLine
        char *token;

        // get first token of command
        singleCommandLine = strtok_r(inputLine, "&", &singleCommandSavePtr);
        if (singleCommandLine == NULL)
            continue;
        // else: there is at least one command

        // resets commands to have not even one single command
        commands.size = 0;

        do
        {
            // get more space for more single commands if necessary
            REALLOC(commands, singleCommands, struct Command *);

            singleCommand = commands.singleCommands[commands.size++];

            // if current command is not allocated
            if (singleCommand == NULL)
            {
                singleCommand = malloc(sizeof(struct Command));
                INIT_ARR((*singleCommand), args, char *);
                commands.singleCommands[commands.size - 1] = singleCommand;
            }

            // reset current single command
            singleCommand->size = 0;

            token = strtok_r(singleCommandLine, " \n\t", &tokenSavePtr);
            if (token == NULL)
            {
                singleCommandLine = strtok_r(NULL, "&", &singleCommandSavePtr);
                continue;
            }

            do
            {
                // add token to single command
                singleCommand->args[singleCommand->size] = token;
                singleCommand->size++;

                // realloc single command's arguments if necessary
                REALLOC((*singleCommand), args, char *);

                // next token
                token = strtok_r(NULL, " \n\t", &tokenSavePtr);
            } while (token != NULL);

            singleCommand->args[singleCommand->size] = NULL; // to be able to call execv (see "man exev")

            // next single command
            singleCommandLine = strtok_r(NULL, "&", &singleCommandSavePtr);
        } while (singleCommandLine != NULL);

        // if last single command is empty, ignore it
        if (singleCommand->size == 0)
            commands.size--;

        int truncated = 0;
        for (int i = 0; i < commands.size; i++)
        {
            singleCommand = commands.singleCommands[i];

            if (!processCommand(singleCommand))
            {
                truncated = 1;
                break;
            }
        }
        // if any command interrupts the shell, exit
        if (truncated)
            break;

        // wait for all children (wait for every command)
        for (int i = 0; i < commands.size; i++)
        {
            wait(NULL);
        }

        if (!batchProcessing)
            printf("\n");
    }
}

int main(int argc, char const *argv[])
{
    inputLine = malloc(sizeof(char) * LINE_MAX);
    filePath = malloc(sizeof(char) * LINE_MAX);

    INIT_ARR(commands, singleCommands, struct Command *);
    INIT_ARR(path, entries, char *);
    path.entries[path.size] = malloc(strlen("/bin") * sizeof(char));
    strcpy(path.entries[path.size++], "/bin");

    exitCode = 0;

    batchProcessing = argc == 2;
    if (argc > 2)
    {
        fprintf(stderr, "An error has occurred\n");
        exitCode = 1;
        goto exit;
    }

    if (batchProcessing)
    {
        FILE *commandsFile = fopen(argv[1], "r");
        if (commandsFile == NULL)
        {
            fprintf(stderr, "An error has occurred\n");
            exitCode = 1;
        }
        else
        {
            runShell(commandsFile);
            fclose(commandsFile);
        }
    }
    else
    {
        runShell(stdin);
    }

exit:
    // free all path entries
    for (int i = 0; i < path.size; i++)
    {
        free(path.entries[i]);
    }
    free(path.entries);

    // free all single commands that were allocated
    for (int i = 0; i < commands.capacity; i++)
    {
        if (commands.singleCommands[i] != NULL)
        {
            free(commands.singleCommands[i]->args);
            free(commands.singleCommands[i]);
        }
    }
    free(commands.singleCommands);

    free(inputLine);
    free(filePath);
    exit(exitCode);
}