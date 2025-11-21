# Cliente FTP Concurrente

Este proyecto implementa un cliente FTP capaz de manejar múltiples sesiones concurrentes mediante procesos creados con `fork()`. El sistema fue probado con el servidor **vsFTPd 3.0.3** en **Debian GNU/Linux 12**, realizando operaciones básicas del protocolo FTP así como transferencias concurrentes de archivos.

## Características principales
- Compatibilidad con comandos FTP fundamentales:  
  `USER`, `PASS`, `LIST`, `CWD`, `RETR`, `STOR`, `PASV`, `PORT`, `QUIT`.
- Manejo de concurrencia mediante procesos independientes.
- Transferencias de archivos en modo activo (PORT) y pasivo (PASV).
- Estructura modular con funciones separadas para conexión, sockets y gestión del protocolo.
- Soporte para sesiones múltiples con manejo de señales (`SIGCHLD`) para prevenir procesos zombie.
