#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#define SEMCOUNT 5
#define EATROUND 3

void philosopher(int id, int left, int right) {
    for(int i = 0; i < EATROUND; i++) {
        if(id % 2 == 0) {
            sem_acquire(left);
            sem_acquire(right);
        }
        else {
            sem_acquire(right);
            sem_acquire(left);
        }
        sleep(40 * id);
        printf(1, "philosopher %d begins with forks %d , %d\n", id, left, right);
        sleep(500);
        sem_release(left);
        sem_release(right);
        printf(1, "philosopher %d done\n", id);
    }
}

void init_game() {
    for(int i = 0; i < SEMCOUNT; i++)
        sem_init(i, 1);
}

void start_game() {
    for(int i = 0; i < SEMCOUNT; i++) {
        int pid = fork();
        
        if(pid == 0) {
            philosopher(i + 1, i, (i + 1) % SEMCOUNT);
            exit();
        }
    }
    
    while (wait());
    exit();
}

int main(int argc, char *argv[])
{
    for (int i = 0; i < SEMCOUNT; i++)
        forks[i] = -1;
    
    init_game();
    start_game();
}
