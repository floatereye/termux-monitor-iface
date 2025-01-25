#include <arpa/inet.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <time.h>

void print_help(const char *progname, int throttle_delay) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Options:\n");
    printf("  -h            Show this help message\n");
    printf("  -v            Enable verbose mode (print interface and IP address)\n");
    printf("  -vv           Enable very verbose mode (only this mode prints output)\n");
    printf("  -D            Run as a daemon\n");
    printf("  -e <command>  Execute a command when interface changes (detached, all parameters after -e passed)\n");
    printf("  -t <seconds>  Set throttle delay for command execution (default: %d seconds)\n", throttle_delay);
    exit(EXIT_SUCCESS);
}

void daemonize() {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS); // Parent exits
    }

    // Child process
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
        exit(EXIT_SUCCESS); // First child exits
    }

    // Second child continues
    umask(0);

    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY); // Redirect stdin
    open("/dev/null", O_WRONLY); // Redirect stdout
    open("/dev/null", O_RDWR);  // Redirect stderr
}

void execute_command(char *command, char **args, const char *ifa_name) {
    pid_t pid = fork();

    if (pid == 0) { // Child process
        size_t len = 0;
        while (args[len]) len++; // Count additional args

        char **exec_args = malloc((len + 2) * sizeof(char *));
        exec_args[0] = command;
        exec_args[1] = (char *)ifa_name;
        for (size_t i = 0; i < len; i++) {
            exec_args[i + 2] = args[i];
        }
        exec_args[len + 2] = NULL;

        execvp(command, exec_args);
        perror("execvp");
        free(exec_args);
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("fork");
    }
}

int main(int argc, char *argv[]) {
    int opt;
    int verbose = 0;
    int very_verbose = 0;
    int daemon = 0;
    int throttle_delay = 5; // Default throttle delay in seconds
    char *exec_command = NULL;
    char **exec_args = NULL;

    while ((opt = getopt(argc, argv, "hvVDe:t:")) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0], throttle_delay);
                break;
            case 'v':
                verbose++;
                break;
            case 'V':
                verbose++;
                break;
            case 'D':
                daemon = 1;
                break;
            case 'e':
                exec_command = optarg;
                exec_args = &argv[optind];
                break;
            case 't':
                throttle_delay = atoi(optarg);
                if (throttle_delay <= 0) {
                    fprintf(stderr, "Throttle delay must be a positive integer.\n");
                    exit(EXIT_FAILURE);
                }
                break;
            default:
                print_help(argv[0], throttle_delay);
                break;
        }
    }

    if (verbose > 1) {
        very_verbose = 1;
    }

    if (daemon) {
        daemonize();
    }

    struct ifaddrs *addrs, *tmp;
    getifaddrs(&addrs);

    if (addrs && addrs->ifa_name) {
        char prev_ifa_name[IFNAMSIZ] = "";
        strncpy(prev_ifa_name, addrs->ifa_name, IFNAMSIZ);
        prev_ifa_name[IFNAMSIZ - 1] = '\0';

        time_t last_exec_time = 0;
        int iface_changed = 0;

        while (1) {
            iface_changed = 0;
            getifaddrs(&addrs);
            for (tmp = addrs; tmp && tmp->ifa_addr; tmp = tmp->ifa_next) {
                if (tmp->ifa_addr->sa_family != AF_INET) continue;
                if (strcmp(tmp->ifa_name, "lo") == 0) continue;

                if (strcmp(prev_ifa_name, tmp->ifa_name) != 0) {
                    iface_changed = 1;
                    strncpy(prev_ifa_name, tmp->ifa_name, IFNAMSIZ);
                    if (very_verbose) {
		        printf("%s\n", tmp->ifa_name);
                    }
                }
            }

            time_t current_time = time(NULL);
            if (iface_changed && difftime(current_time, last_exec_time) >= throttle_delay) {
                if (verbose && !very_verbose) {
                  printf("%s\n", prev_ifa_name);
                }
                if (exec_command) {
                  if (very_verbose) {
                    printf("executing: %s\n", exec_command);
                  }
                  execute_command(exec_command, exec_args, prev_ifa_name);
                }
                last_exec_time = current_time;
            }
            freeifaddrs(addrs);
            sleep(1);
        }
    } else {
        fprintf(stderr, "No interfaces found.\n");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
