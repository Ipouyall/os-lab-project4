#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#define SEMCOUNT 5
#define EATROUND 5

void philosopher(int id, int l, int r) {
    for(int i = 0; i < EATROUND; i++) {
        if(id % 2 == 0) {
            sem_acquire(l);
            sem_acquire(r);
        }
        else {
            sem_acquire(r);
            sem_acquire(l);
        }
        sleep(30 * id);
        printf(1, "philosopher %d begins eating with forks %d , %d\n", id, l + 1, r + 1);
        sleep(300);
        sem_release(l);
        sem_release(r);
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
    init_game();
    start_game();
}
