/*
 * servidor.c — Servidor de chat multithreaded
 * Universidad del Valle de Guatemala · Sistemas Operativos 2026
 *
 * Uso: ./servidor <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "protocolo.h"

/* ─────────────── Constantes ─────────────── */
#define MAX_CLIENTS 100

/* ─────────────── Estructura de cliente ─────────────── */
typedef struct {
    char     username[32];
    char     ip[INET_ADDRSTRLEN];
    char     status[16];
    int      sockfd;
    int      activo;
    time_t   ultimo_mensaje;
} Cliente;

/* ─────────────── Estado global ─────────────── */
static Cliente          lista[MAX_CLIENTS];
static int              num_clientes = 0;
static pthread_mutex_t  mutex_lista  = PTHREAD_MUTEX_INITIALIZER;

/* ─────────────── Utilidades ─────────────── */

/* Envía un paquete completo por un socket */
static int enviar_pkt(int fd, ChatPacket *pkt) {
    ssize_t sent = send(fd, pkt, sizeof(ChatPacket), 0);
    return (sent == (ssize_t)sizeof(ChatPacket)) ? 0 : -1;
}

/* Construye y envía una respuesta simple (OK / ERROR / MSG) */
static void responder(int fd, uint8_t cmd,
                      const char *sender, const char *target,
                      const char *msg)
{
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.command = cmd;
    strncpy(pkt.sender,  sender ? sender : "",  31);
    strncpy(pkt.target,  target ? target : "",  31);
    strncpy(pkt.payload, msg    ? msg    : "", 956);
    pkt.payload_len = (uint16_t)strlen(pkt.payload);
    enviar_pkt(fd, &pkt);
}

/* Busca un cliente por nombre (mutex debe estar tomado) */
static int buscar_por_nombre(const char *username) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (lista[i].activo &&
            strncmp(lista[i].username, username, 31) == 0)
            return i;
    }
    return -1;
}

/* Busca un cliente por IP (mutex debe estar tomado) */
static int buscar_por_ip(const char *ip) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (lista[i].activo &&
            strncmp(lista[i].ip, ip, INET_ADDRSTRLEN - 1) == 0)
            return i;
    }
    return -1;
}

/* Busca un slot libre (mutex debe estar tomado) */
static int slot_libre(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!lista[i].activo) return i;
    }
    return -1;
}

/* ─────────────── Broadcasting ─────────────── */

/* Envía pkt a todos los clientes activos */
static void broadcast_a_todos(ChatPacket *pkt) {
    pthread_mutex_lock(&mutex_lista);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (lista[i].activo) {
            enviar_pkt(lista[i].sockfd, pkt);
        }
    }
    pthread_mutex_unlock(&mutex_lista);
}

/* Notifica a todos que un usuario se desconectó */
static void notificar_desconexion(const char *username) {
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.command = CMD_DISCONNECTED;
    strncpy(pkt.sender,  "SERVER",   31);
    strncpy(pkt.target,  "ALL",      31);
    strncpy(pkt.payload, username,  956);
    pkt.payload_len = (uint16_t)strlen(pkt.payload);
    broadcast_a_todos(&pkt);
}

/* ─────────────── Hilo de inactividad ─────────────── */
static void *hilo_inactividad(void *arg) {
    (void)arg;
    while (1) {
        sleep(10); /* revisar cada 10 segundos */
        time_t ahora = time(NULL);
        pthread_mutex_lock(&mutex_lista);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!lista[i].activo) continue;
            if (strncmp(lista[i].status, STATUS_INACTIVO, 15) == 0) continue;
            if (difftime(ahora, lista[i].ultimo_mensaje) >= INACTIVITY_TIMEOUT) {
                strncpy(lista[i].status, STATUS_INACTIVO, 15);
                printf("[SERVER] %s → INACTIVE por inactividad\n", lista[i].username);
                /* Notificar al propio cliente */
                responder(lista[i].sockfd, CMD_MSG,
                          "SERVER", lista[i].username,
                          "Tu status cambió a INACTIVE");
            }
        }
        pthread_mutex_unlock(&mutex_lista);
    }
    return NULL;
}

/* ─────────────── Handlers por comando ─────────────── */

static void handle_register(int fd, const char *peer_ip, ChatPacket *pkt) {
    char username[32];
    strncpy(username, pkt->sender, 31);
    username[31] = '\0';

    pthread_mutex_lock(&mutex_lista);

    if (buscar_por_nombre(username) >= 0) {
        pthread_mutex_unlock(&mutex_lista);
        responder(fd, CMD_ERROR, "SERVER", username, "Usuario ya existe");
        printf("[SERVER] Registro rechazado: nombre '%s' en uso\n", username);
        return;
    }
    if (buscar_por_ip(peer_ip) >= 0) {
        pthread_mutex_unlock(&mutex_lista);
        responder(fd, CMD_ERROR, "SERVER", username, "IP ya registrada");
        printf("[SERVER] Registro rechazado: IP %s ya en uso\n", peer_ip);
        return;
    }

    int slot = slot_libre();
    if (slot < 0) {
        pthread_mutex_unlock(&mutex_lista);
        responder(fd, CMD_ERROR, "SERVER", username, "Servidor lleno");
        return;
    }

    strncpy(lista[slot].username, username,  31);
    strncpy(lista[slot].ip,       peer_ip,   INET_ADDRSTRLEN - 1);
    strncpy(lista[slot].status,   STATUS_ACTIVO, 15);
    lista[slot].sockfd         = fd;
    lista[slot].activo         = 1;
    lista[slot].ultimo_mensaje = time(NULL);
    num_clientes++;

    pthread_mutex_unlock(&mutex_lista);

    char bienvenida[128];
    snprintf(bienvenida, sizeof(bienvenida), "Bienvenido %s", username);
    responder(fd, CMD_OK, "SERVER", username, bienvenida);
    printf("[SERVER] Usuario registrado: %s desde %s\n", username, peer_ip);
}

static void handle_broadcast(int slot, ChatPacket *pkt) {
    ChatPacket out;
    memset(&out, 0, sizeof(out));
    out.command = CMD_MSG;
    strncpy(out.sender,  pkt->sender,  31);
    strncpy(out.target,  "ALL",        31);
    strncpy(out.payload, pkt->payload, 956);
    out.payload_len = pkt->payload_len;

    pthread_mutex_lock(&mutex_lista);
    lista[slot].ultimo_mensaje = time(NULL);
    if (strncmp(lista[slot].status, STATUS_INACTIVO, 15) == 0)
        strncpy(lista[slot].status, STATUS_ACTIVO, 15);
    pthread_mutex_unlock(&mutex_lista);

    broadcast_a_todos(&out);
    printf("[BROADCAST] %s: %s\n", pkt->sender, pkt->payload);
}

static void handle_direct(int slot, ChatPacket *pkt) {
    pthread_mutex_lock(&mutex_lista);
    lista[slot].ultimo_mensaje = time(NULL);
    if (strncmp(lista[slot].status, STATUS_INACTIVO, 15) == 0)
        strncpy(lista[slot].status, STATUS_ACTIVO, 15);

    int dest = buscar_por_nombre(pkt->target);
    if (dest < 0) {
        int fd = lista[slot].sockfd;
        pthread_mutex_unlock(&mutex_lista);
        responder(fd, CMD_ERROR, "SERVER", pkt->sender,
                  "Destinatario no conectado");
        return;
    }

    ChatPacket out;
    memset(&out, 0, sizeof(out));
    out.command = CMD_MSG;
    strncpy(out.sender,  pkt->sender,  31);
    strncpy(out.target,  pkt->target,  31);
    strncpy(out.payload, pkt->payload, 956);
    out.payload_len = pkt->payload_len;

    int fd_dest = lista[dest].sockfd;
    pthread_mutex_unlock(&mutex_lista);

    enviar_pkt(fd_dest, &out);
    printf("[DM] %s → %s: %s\n", pkt->sender, pkt->target, pkt->payload);
}

static void handle_list(int fd) {
    char buf[957];
    buf[0] = '\0';

    pthread_mutex_lock(&mutex_lista);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!lista[i].activo) continue;
        char entry[64];
        snprintf(entry, sizeof(entry), "%s,%s;",
                 lista[i].username, lista[i].status);
        strncat(buf, entry, sizeof(buf) - strlen(buf) - 1);
    }
    pthread_mutex_unlock(&mutex_lista);

    /* Eliminar último ';' si existe */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == ';') buf[len - 1] = '\0';

    responder(fd, CMD_USER_LIST, "SERVER", "", buf);
}

static void handle_info(int fd, ChatPacket *pkt) {
    pthread_mutex_lock(&mutex_lista);
    int idx = buscar_por_nombre(pkt->target);
    if (idx < 0) {
        pthread_mutex_unlock(&mutex_lista);
        responder(fd, CMD_ERROR, "SERVER", pkt->sender,
                  "Usuario no conectado");
        return;
    }
    char info[128];
    snprintf(info, sizeof(info), "%s,%s",
             lista[idx].ip, lista[idx].status);
    pthread_mutex_unlock(&mutex_lista);

    responder(fd, CMD_USER_INFO, "SERVER", pkt->sender, info);
}

static void handle_status(int slot, ChatPacket *pkt) {
    const char *nuevo = pkt->payload;
    int fd;

    pthread_mutex_lock(&mutex_lista);
    fd = lista[slot].sockfd;

    if (strncmp(nuevo, STATUS_ACTIVO,   6) != 0 &&
        strncmp(nuevo, STATUS_OCUPADO,  4) != 0 &&
        strncmp(nuevo, STATUS_INACTIVO, 8) != 0) {
        pthread_mutex_unlock(&mutex_lista);
        responder(fd, CMD_ERROR, "SERVER", pkt->sender,
                  "Status inválido. Usa: ACTIVE, BUSY, INACTIVE");
        return;
    }

    strncpy(lista[slot].status, nuevo, 15);
    lista[slot].ultimo_mensaje = time(NULL);
    pthread_mutex_unlock(&mutex_lista);

    responder(fd, CMD_OK, "SERVER", pkt->sender, nuevo);
    printf("[STATUS] %s → %s\n", pkt->sender, nuevo);
}

static void handle_logout(int slot) {
    pthread_mutex_lock(&mutex_lista);
    char username[32];
    strncpy(username, lista[slot].username, 31);
    int fd = lista[slot].sockfd;
    lista[slot].activo = 0;
    num_clientes--;
    pthread_mutex_unlock(&mutex_lista);

    responder(fd, CMD_OK, "SERVER", username, "Hasta luego");
    close(fd);
    printf("[SERVER] %s se desconectó limpiamente\n", username);
    notificar_desconexion(username);
}

/* ─────────────── Hilo por cliente ─────────────── */

typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
} ThreadArg;

static void *hilo_cliente(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    int   fd      = ta->fd;
    char  peer_ip[INET_ADDRSTRLEN];
    strncpy(peer_ip, ta->ip, INET_ADDRSTRLEN - 1);
    free(ta);

    ChatPacket pkt;
    int        registrado = 0;
    int        mi_slot    = -1;

    while (1) {
        ssize_t n = recv(fd, &pkt, sizeof(pkt), MSG_WAITALL);
        if (n <= 0) {
            /* Desconexión abrupta */
            if (registrado && mi_slot >= 0) {
                char username[32];
                pthread_mutex_lock(&mutex_lista);
                strncpy(username, lista[mi_slot].username, 31);
                lista[mi_slot].activo = 0;
                num_clientes--;
                pthread_mutex_unlock(&mutex_lista);
                close(fd);
                printf("[SERVER] %s cayó abruptamente\n", username);
                notificar_desconexion(username);
            } else {
                close(fd);
            }
            pthread_exit(NULL);
        }

        if (!registrado) {
            /* Solo se acepta CMD_REGISTER antes de estar registrado */
            if (pkt.command == CMD_REGISTER) {
                handle_register(fd, peer_ip, &pkt);
                /* Verificar si quedó registrado */
                pthread_mutex_lock(&mutex_lista);
                mi_slot = buscar_por_nombre(pkt.sender);
                pthread_mutex_unlock(&mutex_lista);
                if (mi_slot >= 0) registrado = 1;
            } else {
                responder(fd, CMD_ERROR, "SERVER", "", "No registrado");
            }
            continue;
        }

        /* Actualizar último_mensaje para todos los comandos válidos */
        pthread_mutex_lock(&mutex_lista);
        if (mi_slot >= 0 && lista[mi_slot].activo)
            lista[mi_slot].ultimo_mensaje = time(NULL);
        pthread_mutex_unlock(&mutex_lista);

        switch (pkt.command) {
            case CMD_BROADCAST: handle_broadcast(mi_slot, &pkt); break;
            case CMD_DIRECT:    handle_direct(mi_slot, &pkt);    break;
            case CMD_LIST:      handle_list(fd);                  break;
            case CMD_INFO:      handle_info(fd, &pkt);            break;
            case CMD_STATUS:    handle_status(mi_slot, &pkt);     break;
            case CMD_LOGOUT:
                handle_logout(mi_slot);
                pthread_exit(NULL);
                break;
            default:
                responder(fd, CMD_ERROR, "SERVER", pkt.sender,
                          "Comando desconocido");
        }
    }
    return NULL;
}

/* ─────────────── main ─────────────── */

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Puerto inválido: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    /* Crear socket TCP */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return EXIT_FAILURE;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); return EXIT_FAILURE;
    }

    printf("[SERVER] Escuchando en puerto %d...\n", port);

    /* Hilo de inactividad */
    pthread_t tid_inact;
    pthread_create(&tid_inact, NULL, hilo_inactividad, NULL);
    pthread_detach(tid_inact);

    /* Bucle principal: aceptar conexiones */
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int cli_fd = accept(server_fd,
                            (struct sockaddr *)&cli_addr, &cli_len);
        if (cli_fd < 0) {
            perror("accept");
            continue;
        }

        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, peer_ip, sizeof(peer_ip));
        printf("[SERVER] Nueva conexión desde %s\n", peer_ip);

        ThreadArg *ta = malloc(sizeof(ThreadArg));
        ta->fd = cli_fd;
        strncpy(ta->ip, peer_ip, INET_ADDRSTRLEN - 1);

        pthread_t tid;
        pthread_create(&tid, NULL, hilo_cliente, ta);
        pthread_detach(tid);
    }

    close(server_fd);
    return EXIT_SUCCESS;
}
