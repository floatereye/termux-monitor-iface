#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <time.h>

typedef struct {
    int verbose;
    int very_verbose;
    int throttle_delay;
    int daemon;
    char *exec_command;
    char **exec_args;
} Config;

typedef struct {
    char prev_ifa_name[IFNAMSIZ];
    char ifa_name[IFNAMSIZ];
    int changed;
    time_t last_changed_time;
} InterfaceState;

void daemonize() {
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

    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }


    umask(0);

    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
}

void execute_command(InterfaceState *iface_state, Config config) {
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
        exec_args[1] = iface_state->ifa_name;

        for (size_t i = 0; i < len; i++) {
            exec_args[i + 2] = config.exec_args[i];
        }
        exec_args[len + 2] = NULL;

        execvp(config.exec_command, exec_args);
        perror("execvp");
        free(exec_args);
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status)) {
            if (WIFSIGNALED(status)) {
                printf("Child process terminated by signal %d\n", WTERMSIG(status));
            } else {
                printf("Child process terminated abnormally.\n");
                exit(EXIT_FAILURE);
            }
        } else if (WEXITSTATUS(status) != 0) {
            printf("Child process exited with error status %d\n", WEXITSTATUS(status));
        }
    }
}

int fetch_interfaces(struct ifaddrs **addrs) {
    if (getifaddrs(addrs) == -1) {
        perror("getifaddrs");
        return -1;
    }
    return 0;
}

int interface_changed(struct ifaddrs *addrs, InterfaceState *iface_state, Config config) {

   iface_state->changed = 0;
   for (struct ifaddrs *tmp = addrs; tmp && tmp->ifa_addr; tmp = tmp->ifa_next) {
        if (tmp->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(tmp->ifa_name, "lo") == 0) continue;

        strncpy(iface_state->ifa_name, tmp->ifa_name, IFNAMSIZ);

        time_t current_time = time(NULL);
        if (difftime(current_time, iface_state->last_changed_time) < config.throttle_delay) continue;
        iface_state->last_changed_time = current_time;

        if (config.very_verbose) {
            printf("%s\n", iface_state->ifa_name);
        }
        if (strcmp(iface_state->prev_ifa_name, iface_state->ifa_name) != 0) {
            strncpy(iface_state->prev_ifa_name, iface_state->ifa_name, IFNAMSIZ);
            iface_state->changed = 1;
            return 1;
        }
    }
    return 0;
}

void handle_interface_change(InterfaceState *iface_state, Config config) {
    if (! iface_state->changed) return;

    if (config.verbose && !config.very_verbose) {
        printf("%s\n", iface_state->ifa_name);
    }

    if (config.exec_command) {
        execute_command(iface_state, config);
    }
}


void monitor_interfaces(Config config) {
    struct ifaddrs *addrs;
    if (fetch_interfaces(&addrs) == -1 || !addrs || !addrs->ifa_name) {
        fprintf(stderr, "No interfaces found.\n");
        exit(EXIT_FAILURE);
    }

    InterfaceState iface_state = {0};
    strncpy(iface_state.prev_ifa_name, addrs->ifa_name, IFNAMSIZ);
    strncpy(iface_state.ifa_name, addrs->ifa_name, IFNAMSIZ);
    iface_state.last_changed_time = 0;
    iface_state.changed = 0;

    while (1) {
        if (fetch_interfaces(&addrs) == -1) {
            continue;
        }

        interface_changed(addrs, &iface_state, config);
        handle_interface_change(&iface_state, config);

        freeifaddrs(addrs);
        sleep(1);
    }
}

void print_help(const char *progname, Config config) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Options:\n");
    printf("  -v            Enable verbose mode (print interface and IP address)\n");
    printf("  -vv           Enable very verbose mode (print detailed output)\n");
    printf("  -D            Run as a daemon\n");
    printf("  -e <command>  Execute a command when interface changes (detached, all parameters after -e passed)\n");
    printf("  -t <seconds>  Set throttle delay for command execution (default: %d seconds)\n", config.throttle_delay);
    printf("  -h, --help    Show this help message\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {

    Config config = {0};
    config.throttle_delay = 3;

    int option;
    static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}
    };

    int ignore = 0;
    while ((option = getopt_long(argc, argv, "hvvDt:e:", long_options, NULL)) != -1 && !ignore) {
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
            case 'e':
                config.exec_command = optarg;
                config.exec_args = &argv[optind];
                ignore = 1;
                break;
            case 't':
                config.throttle_delay = atoi(optarg);
                if (config.throttle_delay <= 0) {
                    fprintf(stderr, "Throttle delay must be a positive integer.\n");
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
                print_help(argv[0], config);
                break;
            default:
                print_help(argv[0], config);
                break;
        }
    }

    if (config.daemon) {
        daemonize();
    }

    monitor_interfaces(config);

    return EXIT_SUCCESS;
}

