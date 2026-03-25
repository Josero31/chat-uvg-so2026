# Chat UVG — Proyecto Sistemas Operativos 2026

Aplicación de chat cliente-servidor en C con multithreading y sockets TCP.
Implementa el protocolo binario de 1024 bytes acordado por la clase.

---

## Compilación

```bash
make          # compila servidor y cliente
make clean    # limpia binarios
```

**Dependencias:** gcc, make, libpthread (estándar en Linux).

---

## Ejecución

### Servidor
```bash
./servidor <puerto>

# Ejemplo:
./servidor 8080
```

### Cliente
```bash
./cliente <username> <IP_servidor> <puerto>

# Ejemplo local:
./cliente alice 127.0.0.1 8080

# Ejemplo en AWS EC2:
./cliente alice 54.123.45.67 8080
```

---

## Comandos del cliente

| Comando | Descripción |
|---------|-------------|
| `/broadcast <msg>` | Enviar mensaje a todos los usuarios |
| `/msg <usuario> <msg>` | Mensaje privado directo |
| `/status <ACTIVE\|BUSY\|INACTIVE>` | Cambiar status |
| `/list` | Listar usuarios conectados |
| `/info <usuario>` | Ver IP y status de un usuario |
| `/help` | Mostrar ayuda |
| `/exit` | Salir del chat |
| `<texto sin />` | Broadcast automático |

---

## Protocolo — protocolo.h

Struct fijo de **1024 bytes** (packed):

```
uint8_t  command     (1 byte)   — código de operación
uint16_t payload_len (2 bytes)  — longitud útil del payload
char     sender[32]  (32 bytes) — remitente
char     target[32]  (32 bytes) — destinatario ("" = broadcast)
char     payload[957](957 bytes)— contenido del mensaje
```

### Códigos de comando

| Código | Nombre | Dirección |
|--------|--------|-----------|
| 1 | CMD_REGISTER | C → S |
| 2 | CMD_BROADCAST | C → S |
| 3 | CMD_DIRECT | C → S |
| 4 | CMD_LIST | C → S |
| 5 | CMD_INFO | C → S |
| 6 | CMD_STATUS | C → S |
| 7 | CMD_LOGOUT | C → S |
| 8 | CMD_OK | S → C |
| 9 | CMD_ERROR | S → C |
| 10 | CMD_MSG | S → C |
| 11 | CMD_USER_LIST | S → C |
| 12 | CMD_USER_INFO | S → C |
| 13 | CMD_DISCONNECTED | S → C |

---

## Arquitectura del servidor

- **Un hilo principal** acepta conexiones entrantes.
- **Un hilo por cliente** maneja toda la comunicación con ese cliente.
- **Un hilo de inactividad** revisa cada 10 s si algún cliente superó `INACTIVITY_TIMEOUT` (60 s) sin actividad y lo marca `INACTIVE`.
- `pthread_mutex_t` protege la lista de clientes ante accesos concurrentes.

---

## Configuración AWS EC2

En el Security Group de la instancia, habilitar:

```
Tipo:   Custom TCP
Puerto: <puertodelservidor>
Origen: 0.0.0.0/0
```

---

## Interoperabilidad entre grupos

Para que los clientes de un grupo se conecten al servidor de otro:

1. Ambos grupos deben usar **exactamente el mismo `protocolo.h`** (struct idéntico).
2. El cliente apunta a la IP pública de la EC2 del otro grupo y su puerto.
3. El Security Group del servidor debe tener el puerto abierto.

---

## Rúbrica cubierta

| Componente | % |
|------------|---|
| Chateo general con usuarios (broadcast) | 10 |
| Chateo privado con multithreading | 10 |
| Cambio de status | 5 |
| Listado de usuarios e información de usuario | 5 |
| Atención con multithreading (servidor) | 10 |
| Broadcasting y mensajes directos | 10 |
| Registro de usuarios | 5 |
| Liberación de usuarios | 5 |
| Manejo de status (incl. inactividad automática) | 5 |
| Respuesta a solicitudes de información | 5 |
| **Comunicación con otros proyectos** | 30 |
| **TOTAL** | **100** |
