#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

extern int  errno;

int  errexit(const char *format, ...);
int  connectTCP(const char *host, const char *service);
int  passiveTCP(const char *service, int qlen);

#define  LINELEN    128
#define  MAX_CLIENTS  10

/* Estructura para almacenar información de la sesión FTP */
typedef struct {
    int control_sock;
    int data_sock;
    char host[64];
    char service[16];
    pid_t pid;
} ftp_session_t;

ftp_session_t sessions[MAX_CLIENTS];
int session_count = 0;

/* Envia cmds FTP al servidor, recibe respuestas y las despliega */
void sendCmd(int s, char *cmd, char *res) {
    int n;

    n = strlen(cmd);
    char *full_cmd = malloc(n + 3);
    strcpy(full_cmd, cmd);
    full_cmd[n] = '\r';        /* formatear cmd FTP: \r\n al final */
    full_cmd[n+1] = '\n';
    full_cmd[n+2] = '\0';
    
    n = write(s, full_cmd, n+2);   /* envia cmd por canal de control */
    free(full_cmd);
    
    n = read(s, res, LINELEN);    /* lee respuesta del svr */
    res[n] = '\0';        /* despliega respuesta */
    printf("[PID %d] %s", getpid(), res);
}

/* envia cmd PASV; recibe IP,pto del SVR; se conecta al SVR y retorna sock conectado */
int pasivo(int s) {
    int sdata;            /* socket para conexion de datos */
    int nport;            /* puerto (en numeros) en SVR */
    char cmd[128], res[512], *p;  /* comando y respuesta FTP */
    char host[64], port[8];   /* host y port del SVR (como strings) */
    int h1,h2,h3,h4,p1,p2;   /* octetos de IP y puerto del SVR */

    sprintf(cmd, "PASV");
    sendCmd(s, cmd, res);
    p = strchr(res, '(');
    if (p == NULL) {
        printf("Error en respuesta PASV: %s\n", res);
        return -1;
    }
    sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2);
    snprintf(host, 64, "%d.%d.%d.%d", h1,h2,h3,h4);
    nport = p1*256 + p2;
    snprintf(port, 8, "%d", nport);
    sdata = connectTCP(host, port);

    return sdata;
}

/* Función para manejar señales de procesos hijos terminados */
void sigchld_handler(int sig) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void ayuda() {
    printf("Cliente FTP Concurrente. Comandos disponibles:\n \
    connect <host> [puerto] - Conecta a un servidor FTP\n \
    sessions    - muestra sesiones activas\n \
    kill <pid>  - termina una sesion especifica\n \
    help        - despliega este texto\n \
    quit        - sale del programa\n\n");
}

void ayuda_ftp() {
    printf("Comandos FTP disponibles:\n \
    dir         - lista el directorio actual del servidor\n \
    get <archivo>   - copia el archivo desde el servidor al cliente\n \
    put <file>      - copia el archivo desde el cliente al servidor\n \
    pput <file> - copia el archivo desde el cliente al servidor, con PORT\n \
    cd <dir>        - cambia al directorio dir en el servidor\n \
    quit        - finaliza la sesion FTP\n \
    help        - muestra esta ayuda\n\n");
}

/* Función para mostrar sesiones activas */
void mostrar_sesiones() {
    printf("\n=== SESIONES FTP ACTIVAS ===\n");
    int activas = 0;
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].pid > 0) {
            printf("Sesion %d: PID=%d, Host=%s, Puerto=%s\n", 
                   i, sessions[i].pid, sessions[i].host, sessions[i].service);
            activas++;
        }
    }
    if (activas == 0) {
        printf("No hay sesiones activas\n");
    }
    printf("============================\n\n");
}

/* Función para terminar una sesión específica */
void terminar_sesion(pid_t pid) {
    for (int i = 0; i < session_count; i++) {
        if (sessions[i].pid == pid) {
            kill(pid, SIGTERM);
            printf("Sesion con PID %d terminada\n", pid);
            sessions[i].pid = 0;
            return;
        }
    }
    printf("No se encontro sesion con PID %d\n", pid);
}

/* Función que ejecuta cada proceso hijo (sesión FTP) */
void ejecutar_sesion_ftp(char *host, char *service, int session_id) {
    char cmd[128], res[512];
    char data[LINELEN+1];
    char user[32], pass[128], prompt[128], *ucmd, *arg;
    int s, sdata, n;
    FILE *fp;

    printf("[PID %d] Conectando a %s:%s...\n", getpid(), host, service);

    s = connectTCP(host, service);

    n = read(s, res, LINELEN);
    res[n] = '\0';
    printf("[PID %d] %s", getpid(), res);

    /* Autenticación */
    while (1) {
        printf("[PID %d] Please enter your username: ", getpid());
        fflush(stdout);
        
        if (fgets(user, sizeof(user), stdin) == NULL) {
            printf("Error reading username\n");
            exit(1);
        }
        user[strcspn(user, "\n")] = 0;  // Remove newline
        
        sprintf(cmd, "USER %s", user); 
        sendCmd(s, cmd, res);

        printf("[PID %d] Enter your password: ", getpid());
        fflush(stdout);
        
        if (fgets(pass, sizeof(pass), stdin) == NULL) {
            printf("Error reading password\n");
            exit(1);
        }
        pass[strcspn(pass, "\n")] = 0;  // Remove newline
        
        sprintf(cmd, "PASS %s", pass); 
        sendCmd(s, cmd, res);
        
        if (strlen(res) >= 3 && (res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0') == 230) {
            break;
        } else if (strlen(res) >= 3 && (res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0') == 530) {
            printf("[PID %d] Authentication failed, please try again\n", getpid());
        }
    }

    printf("[PID %d] Sesion FTP iniciada correctamente\n", getpid());
    ayuda_ftp();

    /* Loop principal de comandos para esta sesión */
    while (1) {
        printf("[PID %d] ftp> ", getpid());
        fflush(stdout);
        
        if (fgets(prompt, sizeof(prompt), stdin) != NULL) {
            prompt[strcspn(prompt, "\n")] = 0;

            if (strlen(prompt) == 0) continue;

            ucmd = strtok(prompt, " ");

            if (strcmp(ucmd, "dir") == 0) {
                sdata = pasivo(s);
                if (sdata < 0) continue;
                sprintf(cmd, "LIST"); 
                sendCmd(s, cmd, res);
                while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
                    fwrite(data, 1, n, stdout);
                }
                close(sdata);
                n = read(s, res, LINELEN);
                res[n] = '\0';
                printf("[PID %d] %s", getpid(), res);

            } else if (strcmp(ucmd, "get") == 0) {
                arg = strtok(NULL, " ");
                if (arg == NULL) {
                    printf("Uso: get <archivo>\n");
                    continue;
                }
                sdata = pasivo(s);
                if (sdata < 0) continue;
                sprintf(cmd, "RETR %s", arg); 
                sendCmd(s, cmd, res);
                if (strlen(res) >= 3 && (res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0') > 500) {
                    close(sdata);
                    continue;
                }
                fp = fopen(arg, "wb");
                if (fp == NULL) {
                    printf("[PID %d] Error al crear archivo local\n", getpid());
                    close(sdata);
                    continue;
                }
                while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
                    fwrite(data, 1, n, fp);
                }
                fclose(fp);
                close(sdata);
                n = read(s, res, LINELEN);
                res[n] = '\0';
                printf("[PID %d] %s", getpid(), res);

            } else if (strcmp(ucmd, "put") == 0) {
                arg = strtok(NULL, " ");
                if (arg == NULL) {
                    printf("Uso: put <archivo>\n");
                    continue;
                }
                fp = fopen(arg, "rb");
                if (fp == NULL) {
                    printf("[PID %d] Error al abrir archivo local: %s\n", getpid(), arg);
                    continue;
                }
                sdata = pasivo(s);
                if (sdata < 0) {
                    fclose(fp);
                    continue;
                }
                sprintf(cmd, "STOR %s", arg);
                sendCmd(s, cmd, res);

                while ((n = fread(data, 1, LINELEN, fp)) > 0) {
                    if (send(sdata, data, n, 0) < 0) {
                        printf("[PID %d] Error enviando datos\n", getpid());
                        break;
                    }
                }
                fclose(fp);
                close(sdata);
                n = read(s, res, LINELEN);
                res[n] = '\0';
                printf("[PID %d] %s", getpid(), res);

            } else if (strcmp(ucmd, "pput") == 0) {
                arg = strtok(NULL, " ");
                if (arg == NULL) {
                    printf("Uso: pput <archivo>\n");
                    continue;
                }
                fp = fopen(arg, "rb");
                if (fp == NULL) {
                    printf("[PID %d] Error al abrir archivo local: %s\n", getpid(), arg);
                    continue;
                }

                int s1 = passiveTCP("0", 5);
                struct sockaddr_in addrSvr;
                socklen_t alen = sizeof(addrSvr);
                char ip_str[64];

                char lname[64];
                gethostname(lname, 64);
                struct hostent *hent = gethostbyname(lname);
                if (hent == NULL) {
                    printf("[PID %d] Error obteniendo hostname local\n", getpid());
                    fclose(fp);
                    close(s1);
                    continue;
                }
                
                struct in_addr **addr_list = (struct in_addr **)hent->h_addr_list;
                if (addr_list[0] != NULL) {
                    strcpy(ip_str, inet_ntoa(*addr_list[0]));
                } else {
                    printf("[PID %d] Error obteniendo IP local\n", getpid());
                    fclose(fp);
                    close(s1);
                    continue;
                }
                
                // Reemplazar puntos por comas para formato PORT
                char ip_port_format[64];
                strcpy(ip_port_format, ip_str);
                for(int i = 0; i < strlen(ip_port_format); i++) {
                    if(ip_port_format[i] == '.') {
                        ip_port_format[i] = ',';
                    }
                }

                /* Obtener puerto local */
                if (getsockname(s1, (struct sockaddr *)&addrSvr, &alen) < 0) {
                    printf("[PID %d] Error obteniendo puerto local\n", getpid());
                    fclose(fp);
                    close(s1);
                    continue;
                }
                int local_port = ntohs(addrSvr.sin_port);
                int p1 = local_port / 256;
                int p2 = local_port % 256;

                sprintf(cmd, "PORT %s,%d,%d", ip_port_format, p1, p2);
                sendCmd(s, cmd, res);

                sprintf(cmd, "STOR %s", arg); 
                sendCmd(s, cmd, res);
                
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                sdata = accept(s1, (struct sockaddr *)&client_addr, &client_len);
                if (sdata < 0) {
                    printf("[PID %d] Error aceptando conexion de datos\n", getpid());
                    fclose(fp);
                    close(s1);
                    continue;
                }

                while ((n = fread(data, 1, LINELEN, fp)) > 0) {
                    if (send(sdata, data, n, 0) < 0) {
                        printf("[PID %d] Error enviando datos\n", getpid());
                        break;
                    }
                }
                fclose(fp);
                close(sdata);
                close(s1);
                n = read(s, res, LINELEN);
                res[n] = '\0';
                printf("[PID %d] %s", getpid(), res);

            } else if (strcmp(ucmd, "cd") == 0) {
                arg = strtok(NULL, " ");
                if (arg == NULL) {
                    printf("Uso: cd <directorio>\n");
                    continue;
                }
                sprintf(cmd, "CWD %s", arg); 
                sendCmd(s, cmd, res);

            } else if (strcmp(ucmd, "quit") == 0) {
                sprintf(cmd, "QUIT"); 
                sendCmd(s, cmd, res);
                close(s);
                printf("[PID %d] Sesion terminada\n", getpid());
                exit(0);

            } else if (strcmp(ucmd, "help") == 0) {
                ayuda_ftp();

            } else {
                printf("[PID %d] %s: comando no implementado.\n", getpid(), ucmd);
                ayuda_ftp();
            }
        } else {
            // EOF reached, exit session
            sprintf(cmd, "QUIT"); 
            sendCmd(s, cmd, res);
            close(s);
            printf("[PID %d] Sesion terminada (EOF)\n", getpid());
            exit(0);
        }
    }
}

int main(int argc, char *argv[]) {
    char host[64] = "localhost";
    char service[16] = "ftp";
    char prompt[128], *ucmd, *arg;

    /* Configurar manejador de señales para procesos hijos */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    /* Inicializar array de sesiones */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].pid = 0;
    }

    printf("=== CLIENTE FTP CONCURRENTE ===\n");
    ayuda();

    while (1) {
        printf("main> ");
        fflush(stdout);
        
        if (fgets(prompt, sizeof(prompt), stdin) != NULL) {
            prompt[strcspn(prompt, "\n")] = 0;

            if (strlen(prompt) == 0) continue;

            ucmd = strtok(prompt, " ");

            if (strcmp(ucmd, "connect") == 0) {
                arg = strtok(NULL, " ");
                if (arg != NULL) {
                    strcpy(host, arg);
                    strcpy(service, "ftp");

                    char *port_arg = strtok(NULL, " ");
                    if (port_arg != NULL) {
                        strcpy(service, port_arg);
                    }

                    if (session_count < MAX_CLIENTS) {
                        pid_t pid = fork();
                        if (pid == 0) {
                            /* Proceso hijo - ejecutar sesión FTP */
                            ejecutar_sesion_ftp(host, service, session_count);
                            exit(0);
                        } else if (pid > 0) {
                            /* Proceso padre */
                            sessions[session_count].pid = pid;
                            strcpy(sessions[session_count].host, host);
                            strcpy(sessions[session_count].service, service);
                            sessions[session_count].control_sock = -1;
                            session_count++;
                            printf("Nueva sesion iniciada con PID: %d\n", pid);
                            printf("Use 'sessions' para ver sesiones activas\n");
                        } else {
                            perror("fork");
                        }
                    } else {
                        printf("Numero maximo de sesiones alcanzado\n");
                    }
                } else {
                    printf("Uso: connect <host> [puerto]\n");
                }

            } else if (strcmp(ucmd, "sessions") == 0) {
                mostrar_sesiones();

            } else if (strcmp(ucmd, "kill") == 0) {
                arg = strtok(NULL, " ");
                if (arg != NULL) {
                    pid_t pid = atoi(arg);
                    terminar_sesion(pid);
                } else {
                    printf("Uso: kill <pid>\n");
                }

            } else if (strcmp(ucmd, "help") == 0) {
                ayuda();

            } else if (strcmp(ucmd, "quit") == 0) {
                /* Terminar todas las sesiones activas */
                for (int i = 0; i < session_count; i++) {
                    if (sessions[i].pid > 0) {
                        kill(sessions[i].pid, SIGTERM);
                    }
                }
                printf("Saliendo...\n");
                exit(0);

            } else {
                printf("Comando no reconocido: %s\n", ucmd);
                ayuda();
            }
        } else {
            // EOF reached, exit program
            printf("\nSaliendo...\n");
            for (int i = 0; i < session_count; i++) {
                if (sessions[i].pid > 0) {
                    kill(sessions[i].pid, SIGTERM);
                }
            }
            exit(0);
        }
    }

    return 0;
}