/*
 * cliente.c — Cliente de chat (interfaz de terminal con threads)
 * Universidad del Valle de Guatemala · Sistemas Operativos 2026
 *
 * Uso: ./cliente <username> <IP_servidor> <puerto>
 *
 * Comandos:
 *   /broadcast <msg>          → chat general
 *   /msg <usuario> <msg>      → mensaje directo
 *   /status <ACTIVE|BUSY|INACTIVE>
 *   /list
 *   /info <usuario>
 *   /help
 *   /exit
 *   <texto sin barra>         → broadcast automático
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include "protocolo.h"

static int   sockfd      = -1;
static char  my_username[32];
static char  my_status[16];
static int   running = 1;

static pthread_mutex_t mutex_print = PTHREAD_MUTEX_INITIALIZER;

static void timestamp(char *buf, size_t len) {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buf, len, "%H:%M:%S", tm_info);
}

static void print_msg(const char *line) {
    pthread_mutex_lock(&mutex_print);
    printf("\r\033[K%s\n> ", line);
    fflush(stdout);
    pthread_mutex_unlock(&mutex_print);
}

static int enviar_pkt(ChatPacket *pkt) {
    ssize_t sent = send(sockfd, pkt, sizeof(ChatPacket), 0);
    return (sent == (ssize_t)sizeof(ChatPacket)) ? 0 : -1;
}

static int recibir_pkt(ChatPacket *pkt) {
    ssize_t n = recv(sockfd, pkt, sizeof(ChatPacket), MSG_WAITALL);
    return (n == (ssize_t)sizeof(ChatPacket)) ? 0 : -1;
}

static void *hilo_receptor(void *arg) {
    (void)arg;
    ChatPacket pkt;
    char line[1100];
    char ts[10];

    while (running && recibir_pkt(&pkt) == 0) {
        timestamp(ts, sizeof(ts));
        switch (pkt.command) {
            case CMD_OK:
                snprintf(line, sizeof(line), "[%s] OK: %s", ts, pkt.payload);
                print_msg(line);
                break;
            case CMD_ERROR:
                snprintf(line, sizeof(line), "[%s] ERROR: %s", ts, pkt.payload);
                print_msg(line);
                break;
            case CMD_MSG:
                if (strncmp(pkt.target, "ALL", 3) == 0)
                    snprintf(line, sizeof(line), "[%s] [General] %s: %s",
                             ts, pkt.sender, pkt.payload);
                else
                    snprintf(line, sizeof(line), "[%s] [DM %s -> %s]: %s",
                             ts, pkt.sender, pkt.target, pkt.payload);
                print_msg(line);
                if (strncmp(pkt.sender, "SERVER", 6) == 0 &&
                    strstr(pkt.payload, "INACTIVE") != NULL)
                    strncpy(my_status, STATUS_INACTIVO, 15);
                break;
            case CMD_USER_LIST:
                print_msg("---- Usuarios conectados ----");
                {
                    char buf[957];
                    strncpy(buf, pkt.payload, 956); buf[956] = '\0';
                    char *token = strtok(buf, ";");
                    while (token) {
                        char entry[80];
                        snprintf(entry, sizeof(entry), "  * %s", token);
                        print_msg(entry);
                        token = strtok(NULL, ";");
                    }
                }
                print_msg("-----------------------------");
                break;
            case CMD_USER_INFO:
                snprintf(line, sizeof(line), "[%s] Info: %s", ts, pkt.payload);
                print_msg(line);
                break;
            case CMD_DISCONNECTED:
                snprintf(line, sizeof(line), "[%s] << %s se desconecto", ts, pkt.payload);
                print_msg(line);
                break;
            default:
                snprintf(line, sizeof(line), "[%s] cmd=%d desconocido", ts, pkt.command);
                print_msg(line);
        }
    }
    if (running) { print_msg("[!] Conexion con el servidor perdida."); running = 0; }
    return NULL;
}

static void cmd_help(void) {
    printf("---- Comandos ----------------------------------------\n"
           "  /broadcast <msg>              Chat general\n"
           "  /msg <usuario> <msg>          Mensaje privado\n"
           "  /status <ACTIVE|BUSY|INACTIVE>\n"
           "  /list                         Listar usuarios\n"
           "  /info <usuario>               Info de usuario\n"
           "  /help                         Esta ayuda\n"
           "  /exit                         Salir\n"
           "  <texto>                       Broadcast automatico\n"
           "------------------------------------------------------\n> ");
    fflush(stdout);
}

static int procesar_input(char *input) {
    if (strlen(input) == 0) return 0;
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    strncpy(pkt.sender, my_username, 31);

    if (strncmp(input, "/broadcast ", 11) == 0) {
        pkt.command = CMD_BROADCAST;
        strncpy(pkt.payload, input + 11, 956);
        pkt.payload_len = (uint16_t)strlen(pkt.payload);
        enviar_pkt(&pkt);
    } else if (strncmp(input, "/msg ", 5) == 0) {
        char *rest = input + 5, *space = strchr(rest, ' ');
        if (!space) { printf("Uso: /msg <usuario> <mensaje>\n> "); fflush(stdout); return 0; }
        *space = '\0';
        pkt.command = CMD_DIRECT;
        strncpy(pkt.target, rest, 31);
        strncpy(pkt.payload, space + 1, 956);
        pkt.payload_len = (uint16_t)strlen(pkt.payload);
        enviar_pkt(&pkt);
    } else if (strncmp(input, "/status ", 8) == 0) {
        pkt.command = CMD_STATUS;
        strncpy(pkt.payload, input + 8, 956);
        pkt.payload_len = (uint16_t)strlen(pkt.payload);
        if (enviar_pkt(&pkt) == 0) strncpy(my_status, input + 8, 15);
    } else if (strcmp(input, "/list") == 0) {
        pkt.command = CMD_LIST; enviar_pkt(&pkt);
    } else if (strncmp(input, "/info ", 6) == 0) {
        pkt.command = CMD_INFO; strncpy(pkt.target, input + 6, 31); enviar_pkt(&pkt);
    } else if (strcmp(input, "/help") == 0) {
        cmd_help();
    } else if (strcmp(input, "/exit") == 0) {
        pkt.command = CMD_LOGOUT; enviar_pkt(&pkt);
        ChatPacket resp; recibir_pkt(&resp);
        return 1;
    } else if (input[0] != '/') {
        pkt.command = CMD_BROADCAST;
        strncpy(pkt.payload, input, 956);
        pkt.payload_len = (uint16_t)strlen(pkt.payload);
        enviar_pkt(&pkt);
    } else {
        printf("Comando desconocido. /help\n> "); fflush(stdout);
    }
    return 0;
}

static void sig_handler(int sig) {
    (void)sig; running = 0;
    if (sockfd >= 0) {
        ChatPacket pkt; memset(&pkt, 0, sizeof(pkt));
        pkt.command = CMD_LOGOUT; strncpy(pkt.sender, my_username, 31);
        send(sockfd, &pkt, sizeof(pkt), 0); close(sockfd);
    }
    printf("\nHasta luego!\n"); exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 4) { fprintf(stderr, "Uso: %s <username> <IP> <puerto>\n", argv[0]); return EXIT_FAILURE; }
    strncpy(my_username, argv[1], 31);
    const char *server_ip = argv[2];
    int server_port = atoi(argv[3]);
    strncpy(my_status, STATUS_ACTIVO, 15);
    signal(SIGINT, sig_handler); signal(SIGTERM, sig_handler);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return EXIT_FAILURE; }

    struct sockaddr_in srv; memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET; srv.sin_port = htons((uint16_t)server_port);
    if (inet_pton(AF_INET, server_ip, &srv.sin_addr) <= 0) { fprintf(stderr, "IP invalida\n"); return EXIT_FAILURE; }
    if (connect(sockfd, (struct sockaddr *)&srv, sizeof(srv)) < 0) { perror("connect"); return EXIT_FAILURE; }

    ChatPacket reg; memset(&reg, 0, sizeof(reg));
    reg.command = CMD_REGISTER;
    strncpy(reg.sender, my_username, 31); strncpy(reg.payload, my_username, 956);
    reg.payload_len = (uint16_t)strlen(reg.payload);
    if (enviar_pkt(&reg) < 0) { fprintf(stderr, "Error registro\n"); return EXIT_FAILURE; }

    ChatPacket resp;
    if (recibir_pkt(&resp) < 0) { fprintf(stderr, "Error respuesta\n"); return EXIT_FAILURE; }
    if (resp.command == CMD_ERROR) { fprintf(stderr, "Rechazado: %s\n", resp.payload); close(sockfd); return EXIT_FAILURE; }

    printf("==============================================\n");
    printf("  Chat UVG  |  Usuario: %s  |  Status: %s\n", my_username, my_status);
    printf("  Escribe /help para ver los comandos\n");
    printf("==============================================\n> ");
    fflush(stdout);

    pthread_t tid_recv;
    pthread_create(&tid_recv, NULL, hilo_receptor, NULL);
    pthread_detach(tid_recv);

    char line[1024];
    while (running) {
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[--len] = '\0';
        if (procesar_input(line) != 0) break;
        pthread_mutex_lock(&mutex_print); printf("> "); fflush(stdout); pthread_mutex_unlock(&mutex_print);
    }

    running = 0; close(sockfd);
    printf("\nSesion terminada.\n");
    return EXIT_SUCCESS;
}
