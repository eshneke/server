#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

#define MAX_EVENTS 64
#define BUF_SIZE 1024
#define DEFAULT_PORT 8080

static volatile sig_atomic_t shutdown_requested = 0;
static int total_clients = 0;
static int current_clients = 0;

// Обработчик сигнала завершения
void signal_handler(int sig) {
    shutdown_requested = 1;
}

// Установка non-blocking режима
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Форматирование текущего времени
void get_current_time(char *buf, size_t size) {
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_info);
}

// Отправка данных (TCP)
void safe_send(int fd, const char *msg) {
    ssize_t len = strlen(msg);
    ssize_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, msg + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += n;
    }
}

// Основная функция сервера
int main(int argc, char *argv[]) {
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port\n");
            exit(EXIT_FAILURE);
        }
    }

    int tcp_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int udp_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (tcp_sock == -1 || udp_sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(tcp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1 ||
        bind(udp_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(tcp_sock);
        close(udp_sock);
        exit(EXIT_FAILURE);
    }

    if (listen(tcp_sock, SOMAXCONN) == -1) {
        perror("listen");
        close(tcp_sock);
        close(udp_sock);
        exit(EXIT_FAILURE);
    }

    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1");
        close(tcp_sock);
        close(udp_sock);
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = tcp_sock;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, tcp_sock, &ev) == -1) {
        perror("epoll_ctl tcp");
        close(tcp_sock);
        close(udp_sock);
        close(epfd);
        exit(EXIT_FAILURE);
    }

    ev.data.fd = udp_sock;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, udp_sock, &ev) == -1) {
        perror("epoll_ctl udp");
        close(tcp_sock);
        close(udp_sock);
        close(epfd);
        exit(EXIT_FAILURE);
    }

    printf("Server started on port %d\n", port);
    fflush(stdout);

    while (!shutdown_requested) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 1000);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == tcp_sock) {
                // Новое TCP-подключение
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept4(tcp_sock, (struct sockaddr*)&client_addr, &client_len, SOCK_NONBLOCK);
                if (client_fd == -1) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK)
                        perror("accept4");
                    continue;
                }

                ev.events = EPOLLIN | EPOLLRDHUP;
                ev.data.fd = client_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl add client");
                    close(client_fd);
                } else {
                    total_clients++;
                    current_clients++;
                }
                continue;
            }

            if (fd == udp_sock) {
                // UDP-пакет
                char buf[BUF_SIZE];
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                ssize_t n = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0,
                                     (struct sockaddr*)&client_addr, &client_len);
                if (n <= 0) continue;
                buf[n] = '\0';

                char response[BUF_SIZE];
                if (buf[0] == '/') {
                    if (strncmp(buf, "/time", 5) == 0) {
                        get_current_time(response, sizeof(response));
                        strcat(response, "\n");
                    } else if (strncmp(buf, "/stats", 6) == 0) {
                        snprintf(response, sizeof(response),
                                 "Total clients: %d\nCurrent clients: %d\n", total_clients, current_clients);
                    } else if (strncmp(buf, "/shutdown", 9) == 0) {
                        shutdown_requested = 1;
                        strcpy(response, "Shutting down...\n");
                    } else {
                        strcpy(response, "Unknown command\n");
                    }
                } else {
                    // Зеркалирование
                    snprintf(response, sizeof(response), "%s", buf);
                }

                sendto(udp_sock, response, strlen(response), 0,
                       (struct sockaddr*)&client_addr, client_len);
                continue;
            }

            // Обработка TCP-клиента
            char buf[BUF_SIZE];
            ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                // Отключение или ошибка
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    continue;
                close(fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                current_clients--;
                continue;
            }

            buf[n] = '\0';
            char response[BUF_SIZE];
            if (buf[0] == '/') {
                if (strncmp(buf, "/time", 5) == 0) {
                    get_current_time(response, sizeof(response));
                    strcat(response, "\n");
                } else if (strncmp(buf, "/stats", 6) == 0) {
                    snprintf(response, sizeof(response),
                             "Total clients: %d\nCurrent clients: %d\n", total_clients, current_clients);
                } else if (strncmp(buf, "/shutdown", 9) == 0) {
                    shutdown_requested = 1;
                    strcpy(response, "Shutting down...\n");
                } else {
                    strcpy(response, "Unknown command\n");
                }
            } else {
                snprintf(response, sizeof(response), "%s", buf);
            }

            safe_send(fd, response);
            if (shutdown_requested) break;
        }
    }

    printf("Shutting down...\n");
    close(tcp_sock);
    close(udp_sock);
    close(epfd);
    return 0;
}