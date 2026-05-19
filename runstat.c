/* runstat - exec a command, report wall time + peak RSS of the child.
 * usage: runstat <label> -- cmd args...      (reads/writes inherited stdio)
 * build: cc -O2 -o runstat runstat.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <time.h>

int main(int argc, char **argv) {
    int i = 1;
    const char *label = (i < argc) ? argv[i++] : "?";
    if (i < argc && !strcmp(argv[i], "--")) i++;
    if (i >= argc) { fprintf(stderr, "usage: runstat label -- cmd...\n"); return 2; }

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pid_t pid = fork();
    if (pid == 0) { execvp(argv[i], &argv[i]); perror("execvp"); _exit(127); }

    int st; struct rusage ru;
    wait4(pid, &st, 0, &ru);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double wall = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double cpu  = (ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6) +
                  (ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6);
    fprintf(stderr, "%-22s wall=%7.3fs  cpu=%7.3fs  peakRSS=%8ld KB\n",
            label, wall, cpu, ru.ru_maxrss);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}
