#include <getopt.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int verbose;
    int very_verbose;
    int throttle_delay;
    int daemon;
    char *log_file;
    char *exec_command;
    char **exec_args;
} config_t;

typedef struct {
    char prev_ifa_name[IFNAMSIZ];
    char ifa_name[IFNAMSIZ];
    int changed;
    double time_last_poll;
} iface_state_t;


void log_redirect(const char *log_file) {
    int fd = open(log_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("Failed to redirect stdout");
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDERR_FILENO) == -1) {
        perror("Failed to redirect stderr");
        close(fd);
        exit(EXIT_FAILURE);
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    close(fd);
}

void daemon_init(config_t config) {
    if (config.log_file == NULL) {
        fprintf(stderr, "Invalid config_turation: log_file is NULL\n");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    signal(SIGHUP, SIG_IGN);

    umask(0);

    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
        close(fd);
    }
}

void cmd_exec(iface_state_t iface_state, config_t config) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    } else if (pid == 0) {
        size_t len = 0;
        while (config.exec_args[len]) len++;

        char **exec_args = malloc((len + 3) * sizeof(char *));
        if (!exec_args) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        exec_args[0] = config.exec_command;
        exec_args[1] = iface_state.ifa_name;

        for (size_t i = 0; i < len; i++) {
            exec_args[i + 2] = config.exec_args[i];
        }
        exec_args[len + 2] = NULL;

        execvp(config.exec_command, exec_args);
        free(exec_args);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status)) {
            if (WIFSIGNALED(status)) {
                printf("%s Child process terminated by signal %d\n", config.exec_command, WTERMSIG(status));
            } else {
                printf("%s Child process terminated abnormally.\n", config.exec_command);
                exit(EXIT_FAILURE);
            }
        } else if (WEXITSTATUS(status) != 0) {
            printf("%s Child process exited with error status %d\n", config.exec_command, WEXITSTATUS(status));
        }
    }
}

void iface_poll(iface_state_t *iface_state) {

    struct ifaddrs *addrs;
    if (getifaddrs(&addrs) == -1 || !addrs || !addrs->ifa_name) {
        fprintf(stderr, "No interfaces found.\n");
        return;
    }

    iface_state->changed = 0;
    for (struct ifaddrs *tmp = addrs; tmp; tmp = tmp->ifa_next) {

        if (tmp->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(tmp->ifa_name, "lo") == 0) continue;

        snprintf(iface_state->ifa_name, IFNAMSIZ, "%s", tmp->ifa_name);
        if (strcmp(iface_state->prev_ifa_name, iface_state->ifa_name) != 0) {

            snprintf(iface_state->prev_ifa_name, IFNAMSIZ, "%s", iface_state->ifa_name);
            iface_state->changed = 1;
            break;
        }
    }
    freeifaddrs(addrs);
}

void iface_handle_change(iface_state_t iface_state, config_t config) {
    if (! iface_state.changed) return;

    if (config.verbose && !config.very_verbose) {
        printf("%s\n", iface_state.ifa_name);
    }

    if (config.exec_command) {
        cmd_exec(iface_state, config);
    }
}


void iface_monitor(config_t config) {
    struct ifaddrs *addrs;
    if (getifaddrs(&addrs) == -1 || !addrs || !addrs->ifa_name) {
        fprintf(stderr, "No interfaces found.\n");
        return;
    }

    iface_state_t iface_state = {0};
    strncpy(iface_state.ifa_name, addrs->ifa_name, IFNAMSIZ - 1);
    iface_state.ifa_name[IFNAMSIZ - 1] = '\0';
    strncpy(iface_state.prev_ifa_name, iface_state.ifa_name, IFNAMSIZ - 1);
    iface_state.prev_ifa_name[IFNAMSIZ - 1] = '\0';

    iface_state.time_last_poll = time(NULL);
    iface_state.changed = 0;
    freeifaddrs(addrs);

    while (1) {
        if (difftime(time(NULL), iface_state.time_last_poll) < config.throttle_delay) continue;

        if (config.very_verbose) {
           printf("%s\n", iface_state.ifa_name);
        }

        iface_poll(&iface_state);
        iface_state.time_last_poll = time(NULL);

        iface_handle_change(iface_state, config);

        nanosleep((const struct timespec[]){{0, 500000000L}}, NULL);
    }
}


void path_normalize(char *path) {
    char *src = path;
    char *dst = path;

    while (*src) {
        if (src[0] == '.' && src[1] == '/' && (src == path || *(src - 1) == '/')) {
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

char *path_get_absolute(const char progname[], config_t config) {
    static char absolute_path[255];
    static char log_file[255];

    if (!config.log_file) {
        strcat(log_file, progname);
        strcat(log_file, ".log");
    }
    else strcat(log_file, config.log_file);

    if (! getcwd(absolute_path, sizeof(absolute_path))) {
        perror("getcwd failed");
        exit(EXIT_FAILURE);
    }

    size_t len = strlen(absolute_path);
    if (len + 1 + strlen(log_file) >= 255) {
        fprintf(stderr, "Path too long\n");
        exit(EXIT_FAILURE);
    }

    strcat(absolute_path, "/");
    strcat(absolute_path, log_file);

    path_normalize(absolute_path);
    return absolute_path;
}

void print_help(const char progname[], config_t config) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Options:\n");
    printf("  -v            Enable verbose mode (prints interface changes)\n");
    printf("  -vv           Enable very verbose mode (continuously displays current interface)\n");
    printf("  -D            Run as a daemon\n");
    printf("  -l,--logfile  Redirect stdin and stdout to a logfile\n");
    printf("  -e <command>  Execute a command when interface changes\n");
    printf("  -t <seconds>  Set throttle delay for detecting changes (default: %d seconds)\n", config.throttle_delay);
    printf("  -h, --help    Show this help message\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {

    const char *progname = argv[0];

    config_t config = {0};
    config.throttle_delay = 3;

    int option;
    static struct option long_options[] = {
      {"logfile", required_argument, 0, 'l'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };

    int ignore = 0;
    while ((option = getopt_long(argc, argv, "hvvDl:t:e:", long_options, NULL)) != -1 && !ignore) {
        switch (option) {
            case 'v':
                if (config.verbose == 1) {
                    config.very_verbose = 1;
                }
                config.verbose = 1;
                break;
            case 'D':
                config.daemon = 1;
                break;
            case 'l':
                if (optarg[0] == '-') {
                    fprintf(stderr, "%s: option: '%s' requires an argument\n", progname, optarg);
                    print_help(progname, config);
                    exit(EXIT_FAILURE);
                }
                config.log_file = optarg;
            case 'e':
                config.exec_command = optarg;
                config.exec_args = &argv[optind];
                ignore = 1;
                break;
            case 't':
                config.throttle_delay = atoi(optarg);
                if (config.throttle_delay <= 0) {
                    fprintf(stderr, "%s: option: '%s' requires Throttle delay to be a positive integer.\n", progname, optarg);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
                print_help(progname, config);
                break;
            default:
                print_help(progname, config);
                break;
        }
    }


    if (config.log_file || config.daemon) {
        if (config.daemon) {
            daemon_init(config);
        }

        config.log_file = path_get_absolute(progname, config);
        if (! config.log_file) {
            fprintf(stderr, "%s: Failed to determine absolute path\n", progname);
            return EXIT_SUCCESS;
        }
        log_redirect(config.log_file);
    }

    iface_monitor(config);

    return EXIT_SUCCESS;
}

