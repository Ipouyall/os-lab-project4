#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[])
{
    printf(1,"child--> pid = %d and parent--> pid = %d\n",
            getpid(), get_parent_pid());
    int pid, pid1;
    pid = fork();
    if (pid < 0)
    {
        printf(1,"Eror in fork()\n");
        while (wait() != -1);
        exit();
    }
    else if (pid == 0)
    {
        printf(1,"child--> pid = %d and parent--> pid = %d\n",
               getpid(), get_parent_pid());
        pid1 = fork();
        if (pid1 < 0)
        {
            printf(1,"Eror in fork()\n");
            while (wait() != -1);
            exit();
        }
        else if (pid1 == 0)
        {
            printf(1,"child--> pid = %d and parent--> pid = %d\n",
                getpid(), get_parent_pid());
        }
    }
    while (wait() != -1);
    exit();
}