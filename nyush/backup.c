
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h> 

// Assuming no more than 100 suspended jobs
#define MAX_JOBS 100  


typedef struct {
    pid_t pid;           
    char command[1024];  
    bool active;
} Job;

Job jobs[MAX_JOBS];  
int job_count = 0;    


void print_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].active) {
            printf("[%d] %s\n", i + 1, jobs[i].command);
        }
    }
}

void print_prompt() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        // Only extract the base name from the current working directory
        char* base = basename(cwd);
        // Handling for root directory
        // strcmp(): compare whether two strings are identical -> 0
        printf("[nyush %s]$ ", strcmp(base, "/") == 0 ? "/" : base);
        fflush(stdout);
    }
}

char* read_line() {
    char *line = NULL;
    size_t bufferlen = 0;
    ssize_t linelen;

    // getline(): allocate buffer pointed to by line to fit the input line
    linelen = getline(&line, &bufferlen, stdin);

    if (linelen == -1) {
        // Check if EOF was reached on stdin (Ctrl-D)
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
    //delimiters: space, tab, carriage return, new line
    char *delimiters = " \t\r\n";
    // strtok(): break string into tokens based on delimiters
    char *token = strtok(line, delimiters);

    while (token != NULL) {
        tokens[length] = token;
        length++;

        // dynamic array resizing: reach the capacity -> double the capacity
        if (length >= capacity) {
            capacity = (int) (capacity * 2);
            tokens = realloc(tokens, capacity * sizeof(char*));
        }

        token = strtok(NULL, delimiters);
    }

    tokens[length] = NULL;
    return tokens;
}

void fg_command(char **args) {
    if (args[1] == NULL || args[2] != NULL) {
        fprintf(stderr, "Error: invalid command\n");
        return;
    }

    int jobIndex = atoi(args[1]) - 1;
    if (jobIndex < 0 || jobIndex >= job_count || !jobs[jobIndex].active) {
        fprintf(stderr, "Error: invalid job\n");
        return;
    }

    pid_t pid = jobs[jobIndex].pid;
    jobs[jobIndex].active = false; // Mark job as inactive

    // Continue the job in the foreground
    kill(pid, SIGCONT);
    int status;
    waitpid(pid, &status, WUNTRACED);
}

int execute_builtin(char **args) {
    if (strcmp(args[0], "cd") == 0) {
        // Check for exactly ONE argument
        if (args[1] == NULL || args[2] != NULL) {
            fprintf(stderr, "Error: invalid command\n");
        } else {
            // int chdir(const char *path); On success, 0 is returned. Otherwise, -1
            if (chdir(args[1]) != 0) {
                // Print specific error message if the directory does not exist
                fprintf(stderr, "Error: invalid directory\n");
            }
        }
        return 1;
    } else if (strcmp(args[0], "exit") == 0) {
        // Check for no arguments
        if (args[1] != NULL) {
            fprintf(stderr, "Error: invalid command\n");
            return 1;
        }
        for (int i = 0; i < job_count; ++i) {
            if (jobs[i].active) {
                fprintf(stderr, "Error: there are suspended jobs\n");
                return 1; // Prevent exit
            }
        }
        exit(0);
    } else if (strcmp(args[0], "jobs") == 0) {
        if (args[1] != NULL) {
            fprintf(stderr, "Error: invalid command\n");
        } else {
            list_jobs();
        }
        return 1;
    } else if (strcmp(args[0], "fg") == 0) {
        fg_command(args);
        return 1;
    }
    return 0; // Not a built-in command
}

void add_job(pid_t pid, char *command) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].command, command, sizeof(jobs[job_count].command) - 1);
        jobs[job_count].active = true;
        job_count++;
    }
}

void execute_command_with_pipe(char *cmd1[], char *cmd2[]) {
    int pipefd[2];
    pipe(pipefd); // Create a pipe

    pid_t pid1 = fork();
    if (pid1 == 0) {
        // First child: executes cmd1
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to the pipe's write end
        close(pipefd[0]); // Close unused read end
        close(pipefd[1]); // Close write end after duplicating

        if (execvp(cmd1[0], cmd1) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        // Second child: executes cmd2
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to the pipe's read end
        close(pipefd[1]); // Close unused write end
        close(pipefd[0]); // Close read end after duplicating

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

    int in_redirect = -1, out_redirect = -1, append_redirect = -1;
    char *input_file = NULL, *output_file = NULL;

    // Parse command for I/O redirection symbols
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            input_file = args[i + 1];
            in_redirect = i;
        } else if (strcmp(args[i], ">") == 0) {
            output_file = args[i + 1];
            out_redirect = i;
        } else if (strcmp(args[i], ">>") == 0) {
            output_file = args[i + 1];
            append_redirect = i;
        }
    }

    // Remove redirection symbols and filenames from args
    if (in_redirect != -1) args[in_redirect] = NULL;
    if (out_redirect != -1) args[out_redirect] = NULL;
    if (append_redirect != -1) args[append_redirect] = NULL;

    pid_t child_pid = fork();

    if (child_pid == 0) {
        // Input redirection
        if (in_redirect != -1) {
            int fd_in = open(input_file, O_RDONLY);
            if (fd_in < 0) {
                fprintf(stderr, "Error: invalid file\n");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        // Output redirection
        if (out_redirect != -1 || append_redirect != -1) {
            int flags = O_WRONLY | O_CREAT | (out_redirect != -1 ? O_TRUNC : O_APPEND);
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
        add_job(child_pid, args[0]);
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