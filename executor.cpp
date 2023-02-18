#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
//#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

#include "err.h"

#define MAX_N_TASKS 4096
#define MAXSIZE 512

int next_id, ***pipefd, last = -1, *status, counter;
pthread_t *threads;
pthread_mutex_t inner_mutex, outer_mutex, safeguard, **sem;
char ***memo, ***buffer;
pid_t *pid;

struct arg_struct1 {
    int i;
    char **parts;
};

struct arg_struct2 {
    int i, mode;
};

void *reader(void *data) {
    struct arg_struct2 *args = (struct arg_struct2 *)data;
    int i = args->i, mode = args->mode;
    free(data);

    char *last_line = (char*)malloc(sizeof(char) * MAXSIZE);
    FILE *f = fdopen(pipefd[i][mode][0], "r");
    while (read_line(last_line, MAXSIZE, f)) {
        pthread_mutex_lock(&sem[i][mode]);
        size_t len = strlen(last_line), found = 0, j;

        if (len == 0) {
            if (feof(f) && strlen(&buffer[i][mode][0]) > 0) {
                strcpy(&memo[i][mode][0], &buffer[i][mode][0]);
                buffer[i][mode][0] = '\0';
            }
            pthread_mutex_unlock(&sem[i][mode]);
            continue;
        }

        if (feof(f)) found = len;

        bool ok = false;
        for (j = len - 1; j > 0; --j) {
            if (found > 0 && last_line[j] == '\n') {
                if (found <= len - 1)
                    last_line[found] = '\0';
                if (j == len - 1) memo[i][mode][0] = '\0';
                else {
                    strcpy(&memo[i][mode][0], last_line + j + 1);
                    if (found <= len - 2)
                        strcpy(&buffer[i][mode][0], last_line + found + 1);
                    else buffer[i][mode][0] = '\0';
                }
                pthread_mutex_unlock(&sem[i][mode]);
                ok = true;
                break;
            }
            if (last_line[j] == '\n') found = j;
        }

        if (ok) continue;

        if (found > 0) {
            if (last_line[0] == '\n') {
                if (found <= len - 1)
                    last_line[found] = '\0';
                strcpy(&memo[i][mode][0], last_line + 1);
                if (found <= len - 2)
                    strcpy(&buffer[i][mode][0], last_line + found + 1);
                else buffer[i][mode][0] = '\0';
                pthread_mutex_unlock(&sem[i][mode]);
                continue;
            } else {
                if (found <= len - 1)
                    last_line[found] = '\0';
                strcpy(&memo[i][mode][0], &buffer[i][mode][0]);
                strcpy(&memo[i][mode][strlen(&buffer[i][mode][0])], last_line);
                if (found <= len - 2)
                    strcpy(&buffer[i][mode][0], last_line + found + 1);
                else buffer[i][mode][0] = '\0';
                pthread_mutex_unlock(&sem[i][mode]);
                continue;
            }
        }

        if (last_line[0] == '\n') {
            strcpy(&memo[i][mode][0], &buffer[i][mode][0]);
            strcpy(&buffer[i][mode][0], last_line + 1);
            pthread_mutex_unlock(&sem[i][mode]);
            continue;
        }

        strcpy(&buffer[i][mode][strlen(&buffer[i][mode][0])], last_line);
        pthread_mutex_unlock(&sem[i][mode]);
    }

    close(pipefd[i][mode][0]);

    pthread_exit(NULL);
}

void *forker(void *data) {
    struct arg_struct1 *args = (struct arg_struct1 *)data;
    int i = args->i;
    char **parts = args->parts;
    free(data);
    int rc = fork();
    if (rc < 0) exit(1);
    else if (rc == 0) {
        pid[i] = getpid();
        printf("Task %d started: pid %d.\n", i, getpid());

        close(pipefd[i][0][0]);
        close(pipefd[i][1][0]);
        dup2(pipefd[i][0][1], STDOUT_FILENO);
        dup2(pipefd[i][1][1], STDERR_FILENO);
        execvp(parts[1], &parts[1]);
     
        close(pipefd[i][0][1]);
        close(pipefd[i][1][1]);
    } else {
        close(pipefd[i][0][1]);
        close(pipefd[i][1][1]);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        
        struct arg_struct2 *args1 = (arg_struct2*)malloc(sizeof(struct arg_struct2));
        args1->i = i;
        args1->mode = 0;
        pthread_create(&threads[3 * i], &attr, reader, args1);
        struct arg_struct2 *args2 = (arg_struct2*)malloc(sizeof(struct arg_struct2));
        args2->i = i;
        args2->mode = 1;
        pthread_create(&threads[3 * i + 1], &attr, reader, args2);

        int _rc = fork();
        if (_rc == 0) {
            int stat;
            waitpid(rc, &stat, 0);
        pthread_mutex_lock(&safeguard);
        if (counter == 0) {
            last = -1;
            pthread_mutex_lock(&outer_mutex);
        }
        counter++;
        pthread_mutex_unlock(&safeguard);
        pthread_join(threads[3 * i], NULL);
        pthread_join(threads[3 * i + 1], NULL);
        if (last != -1)
            pthread_join(threads[3 * last + 2], NULL);
        
        printf("Task %d ended: ", i);
        if (WIFSIGNALED(stat)) {
            printf("signalled.\n");
        } else {
            printf("status %d.\n", WEXITSTATUS(stat));
        }

        pthread_mutex_lock(&safeguard);
        last = i;
        counter--;
        if (counter == 0) pthread_mutex_unlock(&outer_mutex);
        pthread_mutex_unlock(&safeguard);
        }
        return NULL;
    }
}

int main() {
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    char *command = (char*)malloc(sizeof(char) * MAXSIZE);
    char **parts;

    memo = (char***)malloc(sizeof(char**) * MAX_N_TASKS);
    for (int i = 0; i < MAX_N_TASKS; ++i) memo[i] = (char**)malloc(sizeof(char*) * 2);
    for (int i = 0; i < MAX_N_TASKS; ++i) for (int j = 0; j < 2; ++j)
        memo[i][j] = (char*) malloc(sizeof(char) * MAXSIZE);

    buffer = (char***)malloc(sizeof(char**) * MAX_N_TASKS);
    for (int i = 0; i < MAX_N_TASKS; ++i) buffer[i] = (char**)malloc(sizeof(char*) * 2);
    for (int i = 0; i < MAX_N_TASKS; ++i) for (int j = 0; j < 2; ++j)
        buffer[i][j] = (char*) malloc(sizeof(char) * MAXSIZE);

    threads = (pthread_t*)malloc(sizeof(pthread_t) * 3 * MAX_N_TASKS);
    pid = (pid_t*)malloc(sizeof(pid_t) * MAX_N_TASKS);

    sem = (pthread_mutex_t**)malloc(sizeof(pthread_mutex_t*) * MAX_N_TASKS);
    for (int i = 0; i < MAX_N_TASKS; ++i) sem[i] = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * 2);

    status = (int*)malloc(sizeof(int) * MAX_N_TASKS);
    pipefd = (int***)malloc(sizeof(int**) * MAX_N_TASKS);
    for (int i = 0; i < MAX_N_TASKS; ++i) pipefd[i] = (int**)malloc(sizeof(int*) * 2);
    for (int i = 0; i < MAX_N_TASKS; ++i) for (int j = 0; j < 2; ++j) pipefd[i][j] = (int*)malloc(sizeof(int) * 2);

    pthread_mutex_init(&inner_mutex, NULL);
    pthread_mutex_init(&outer_mutex, NULL);
    pthread_mutex_init(&safeguard, NULL); 

    pthread_mutex_lock(&outer_mutex);
    while (read_line(command, MAXSIZE, stdin)) {
        parts = split_string(command);
        if (parts[0][0] == 'r') {
            pipe(pipefd[next_id][0]);
            pipe(pipefd[next_id][1]);

            pthread_mutex_init(&sem[next_id][0], NULL);
            pthread_mutex_init(&sem[next_id][1], NULL);

            pthread_attr_t attr;
            pthread_attr_init(&attr);

            struct arg_struct1 *args = (arg_struct1*)malloc(sizeof(struct arg_struct1));
            args->i = next_id;
            args->parts = parts;
            pthread_create(&threads[next_id++], &attr, forker, args);
        } else if (parts[0][0] == 'o') {
            int i = atoi(parts[1]);
            pthread_mutex_lock(&sem[i][0]);
            printf("Task %d stdout: '%s'.\n", i, &memo[i][0][0]);
            pthread_mutex_unlock(&sem[i][0]);
        } else if (parts[0][0] == 'e') {
            int i = atoi(parts[1]);
            pthread_mutex_lock(&sem[i][1]);
            printf("Task %d stderr: '%s'.\n", i, &memo[i][1][0]);
            pthread_mutex_unlock(&sem[i][1]);
        } else if (parts[0][0] == 'k') {
            int i = atoi(parts[1]);
            kill(pid[i], SIGINT);
        } else if (parts[0][0] == 's')
            usleep(atoi(parts[1]) * 1000);
        
        pthread_mutex_unlock(&outer_mutex);
        pthread_mutex_lock(&outer_mutex);

          if (last != - 1)
           pthread_join(threads[3 * last + 2], NULL);

        if (parts[0][0] == 'q') break;
    }

    free(command);
    pthread_mutex_destroy(&inner_mutex);
    pthread_mutex_destroy(&outer_mutex);
    pthread_mutex_destroy(&safeguard);
    for (int i = 0; i < next_id; ++i) {
        pthread_mutex_destroy(&sem[i][0]);
        pthread_mutex_destroy(&sem[i][1]);
    }

    for (int i = 0; i < MAX_N_TASKS; ++i) for (int j = 0; j < 2; ++j)
        free(memo[i][j]);
    for (int i = 0; i < MAX_N_TASKS; ++i) free(memo[i]);
    free(memo);

    for (int i = 0; i < MAX_N_TASKS; ++i) for (int j = 0; j < 2; ++j)
        free(buffer[i][j]);
    for (int i = 0; i < MAX_N_TASKS; ++i) free(buffer[i]);
    free(buffer);

    free(threads);
    free(pid);

    for (int i = 0; i < MAX_N_TASKS; ++i) free(sem[i]);
        free(sem);
    for (int i = 0; i < MAX_N_TASKS; ++i) for (int j = 0; j < 2; ++j) free(pipefd[i][j]);
        for (int i = 0; i < MAX_N_TASKS; ++i) free(pipefd[i]);
            free(pipefd);

    free_split_string(parts);
}