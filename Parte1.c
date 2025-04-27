// Lab2_shm_final_fix.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define MAX_PEDIDOS 5
#define PEDIDO_LEN 64

// Estructura del pedido
typedef struct {
    int cliente_id;
    char pedido[PEDIDO_LEN];
    int estado; // 0: libre, 1: recibido, 2: preparado
} Pedido;

// Estructura completa de memoria compartida
typedef struct {
    Pedido cola[MAX_PEDIDOS];
    int head;  // índice del próximo pedido a atender
    int tail;  // índice donde el cliente escribe
} BufferCompartido;

// Llaves compartidas
#define SHM_KEY 0x1234
#define SEM_KEY 0x5678

// Semáforos: 0 = mutex, 1 = espacio disponible, 2 = pedidos disponibles
int sem_id;

void init_semaforos() {
    sem_id = semget(SEM_KEY, 3, IPC_CREAT | 0666);
    if (sem_id < 0) {
        perror("semget");
        exit(1);
    }
    semctl(sem_id, 0, SETVAL, 1);          // mutex
    semctl(sem_id, 1, SETVAL, MAX_PEDIDOS); // espacios disponibles
    semctl(sem_id, 2, SETVAL, 0);           // pedidos disponibles
}

// Operación P (wait)
void sem_wait(int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    semop(sem_id, &op, 1);
}

// Operación V (signal)
void sem_signal(int sem_num) {
    struct sembuf op = {sem_num, +1, 0};
    semop(sem_id, &op, 1);
}

void cliente(int id) {
    int shm_id = shmget(SHM_KEY, sizeof(BufferCompartido), 0666);
    if (shm_id < 0) {
        perror("shmget cliente");
        exit(1);
    }

    BufferCompartido *buffer = (BufferCompartido *) shmat(shm_id, NULL, 0);

    char comida[PEDIDO_LEN];

    while (1) {
        printf("Cliente %d - Ingrese su pedido ('salir' para terminar): ", id);
        fgets(comida, PEDIDO_LEN, stdin);
        comida[strcspn(comida, "\n")] = 0;

        if (strcmp(comida, "salir") == 0) {
            printf("Cliente %d desconectándose...\n", id);
            break;
        }

        sem_wait(1); // Esperar espacio disponible
        sem_wait(0); // Entrar a sección crítica (mutex)

        int posicion = buffer->tail;
        Pedido *nuevo_pedido = &buffer->cola[posicion];
        nuevo_pedido->cliente_id = id;
        strncpy(nuevo_pedido->pedido, comida, PEDIDO_LEN);
        nuevo_pedido->estado = 1; // Pedido recibido

        printf("Cliente %d: Pedido '%s' enviado.\n", id, comida);

        buffer->tail = (buffer->tail + 1) % MAX_PEDIDOS;

        sem_signal(0); // Salir de sección crítica
        sem_signal(2); // Hay un nuevo pedido disponible

        // Ahora esperar que su pedido sea preparado
        printf("Cliente %d: Esperando que el pedido sea preparado...\n", id);

        // Aquí en vez de quedarse esperando en una posición fija, buscamos por cliente_id
        int pedido_listo = 0;
        while (!pedido_listo) {
            sem_wait(0); // mutex
            for (int i = 0; i < MAX_PEDIDOS; i++) {
                if (buffer->cola[i].cliente_id == id && buffer->cola[i].estado == 2) {
                    printf("Cliente %d: ¡Tu pedido '%s' está listo!\n", id, buffer->cola[i].pedido);
                    // Marcar como leído para no volverlo a leer
                    buffer->cola[i].estado = 0;
                    pedido_listo = 1;
                    break;
                }
            }
            sem_signal(0); // mutex
            usleep(100000); // 100ms
        }
    }

    shmdt(buffer);
}

void cocina() {
    int shm_id = shmget(SHM_KEY, sizeof(BufferCompartido), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget cocina");
        exit(1);
    }

    BufferCompartido *buffer = (BufferCompartido *) shmat(shm_id, NULL, 0);

    init_semaforos();

    printf("Cocina lista para atender pedidos...\n");

    while (1) {
        sem_wait(2); // Esperar pedidos disponibles
        sem_wait(0); // Entrar a sección crítica (mutex)

        Pedido *pedido = &buffer->cola[buffer->head];

        if (pedido->estado == 1) {
            printf("Cocina: preparando pedido de cliente %d: %s\n", pedido->cliente_id, pedido->pedido);
            sleep(2); // Simular preparación
            pedido->estado = 2; // Pedido preparado
            printf("Cocina: pedido de cliente %d listo: %s\n", pedido->cliente_id, pedido->pedido);

            buffer->head = (buffer->head + 1) % MAX_PEDIDOS;
        }

        sem_signal(0); // Salir de sección crítica
        sem_signal(1); // Hay un espacio libre
    }

    shmdt(buffer);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s [cliente ID | cocina]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else {
        int id = atoi(argv[1]);
        cliente(id);
    }

    return 0;
}
