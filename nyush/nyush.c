
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h> 

void print_prompt() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        // Only extract the base name from current working directory
        char* base = basename(cwd);
        // Special handling for root directory
        // strcmp(): compare whether two strings are equal
        printf("[nyush %s]$ ", strcmp(base, "/") == 0 ? "/" : base);
        fflush(stdout);
    }
}

char* read_line() {
    char *line = NULL;
    size_t buflen = 0;
    ssize_t linelen;

    // getline(): read the line from standard input
    linelen = getline(&line, &buflen, stdin);

    if (linelen == -1) {
        // if reached the end of the file (Ctrl-D)
        if (feof(stdin)) {
            printf("\n");
            exit(EXIT_SUCCESS);
        } else {
            // Other errors occurred
            perror("getline");
            exit(EXIT_FAILURE);
        }
    }

    return line;
}

char** split_line(char *line) {
    int length = 0;
    int capacity = 16;
    char **tokens = malloc(capacity * sizeof(char*));
    // space, tab, carriage return, new line
    char *delimiters = " \t\r\n";
    // split the line into tokens by splitting delimiters
    char *token = strtok(line, delimiters);

    while (token != NULL) {
        tokens[length] = token;
        length++;

        if (length >= capacity) {
            capacity = (int) (capacity * 2);
            tokens = realloc(tokens, capacity * sizeof(char*));
        }

        token = strtok(NULL, delimiters);
    }

    tokens[length] = NULL;
    return tokens;
}

int execute_builtin(char **args) {
    if (strcmp(args[0], "cd") == 0) {
        // Check for only one argument
        if (args[1] == NULL || args[2] != NULL) {
            fprintf(stderr, "Error: invalid command\n");
        } else {
            if (chdir(args[1]) != 0) {
                // If the directory does not exist
                fprintf(stderr, "Error: invalid directory\n");
            }
        }
        return 1;
    } else if (strcmp(args[0], "exit") == 0) {
        // Check for no arguments
        if (args[1] != NULL) {
            fprintf(stderr, "Error: invalid command\n");
        } else {
            exit(0);
        }
        return 1;
    }
    return 0;
}

void execute_command_with_pipe(char *cmd1[], char *cmd2[]) {
    int pipefd[2];
    pipe(pipefd); // Create a pipe

    pid_t pid1 = fork();
    if (pid1 == 0) {
        // First child executes command 1
        // Redirect stdout to the pipe's write end
        dup2(pipefd[1], STDOUT_FILENO); 
        // Close unused read end
        close(pipefd[0]);
        // Close write end after duplicating
        close(pipefd[1]); 

        if (execvp(cmd1[0], cmd1) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // Second child executes command 2
        // Redirect stdin to the pipe's read end
        dup2(pipefd[0], STDIN_FILENO);
        // Close unused write end
        close(pipefd[1]);
        // Close read end after duplicating
        close(pipefd[0]);

        if (execvp(cmd2[0], cmd2) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }

    // Parent process
    close(pipefd[0]); // Close both ends of the pipe
    close(pipefd[1]);
    waitpid(pid1, NULL, 0); // Wait for both children to terminate
    waitpid(pid2, NULL, 0);
}


void command_exec(char **args) {
    // Check for a pipe in the command
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            args[i] = NULL; // Split args into two parts at the pipe symbol
            execute_command_with_pipe(args, &args[i + 1]);
            return;
        }
    }
    if (execute_builtin(args)) {
        return;
    }

    // Track the presence and positions of I/O redirection symbols 
    // and the associated file names.
    int in_redir_pos = -1;
    int out_redir_pos = -1; 
    int append_redir_pos = -1;
    char *input_file = NULL;
    char *output_file = NULL;

    // Parse command for I/O redirection symbols
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            input_file = args[i + 1];
            in_redir_pos = i;
        } else if (strcmp(args[i], ">") == 0) {
            output_file = args[i + 1];
            out_redir_pos = i;
        } else if (strcmp(args[i], ">>") == 0) {
            output_file = args[i + 1];
            append_redir_pos = i;
        }
    }

    // Remove redirection symbols and filenames from args
    if (in_redir_pos != -1) args[in_redir_pos] = NULL;
    if (out_redir_pos != -1) args[out_redir_pos] = NULL;
    if (append_redir_pos != -1) args[append_redir_pos] = NULL;

    pid_t child_pid = fork();

    if (child_pid == 0) {
        // Input redirection
        if (in_redir_pos != -1) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0) {
                fprintf(stderr, "Error: invalid file\n");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        // Output redirection
        if (out_redir_pos != -1 || append_redir_pos != -1) {
            int flags = O_WRONLY | O_CREAT | (out_redir_pos != -1 ? O_TRUNC : O_APPEND);
            int fd_out = open(output_file, flags, 0644);
            if (fd_out < 0) {
                fprintf(stderr, "Error: invalid file\n");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        if (execvp(args[0], args) == -1) {
            perror("error");
            exit(EXIT_FAILURE);
        }
    } else if (child_pid > 0) {
        // Parent process
        int status;
        do {
            waitpid(child_pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    } else {
        // Fork failed
        perror("error");
    }
}

int main() {
    while (true) {
        print_prompt();

        char *line = read_line();
        char **tokens = split_line(line);

        if (tokens[0] != NULL) {
            command_exec(tokens);
        }

        free(tokens);
        free(line);
    }
}