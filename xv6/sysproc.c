#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_get_parent_pid(void)
{
  return get_parent_pid();
}

// SYSCALL to find the largest prime factor of a number
int
sys_find_largest_prime_factor(void)
{
  int number = myproc()->tf->ebx;
  cprintf("Kernel: sys_find_largest_prime_factor(%d) is called\n", number);
  cprintf("        now calling find_largest_prime_factor(%d)\n", number);
  return find_largest_prime_factor(number);
}

// SYSCALL to get callers pids
void
sys_get_callers(void)
{
  int number;
  argint(0, &number);
  cprintf("Kernel: sys_get_callers(%d) is called\n", number);
  cprintf("        now calling get_callers(%d)\n", number);
  get_callers(number);
}

void
sys_set_proc_queue(void)
{
  int pid, queue;
  argint(0, &pid);
  argint(1, &queue);
  set_proc_queue(pid, queue);
}

void
sys_set_lottery_params(void)
{
  int pid, ticket_chance;
  argint(0, &pid);
  argint(1, &ticket_chance);
  set_lottery_params(pid, ticket_chance);
}

void
sys_set_a_proc_bjf_params(void)
{
  int pid, priority_ratio, arrival_time_ratio, executed_cycle_ratio;
  argint(0, &pid);
  argint(1, &priority_ratio);
  argint(2, &arrival_time_ratio);
  argint(3, &executed_cycle_ratio);
  set_a_proc_bjf_params(pid, priority_ratio, arrival_time_ratio, executed_cycle_ratio);
}

void
sys_set_all_bjf_params(void)
{
  int priority_ratio, arrival_time_ratio, executed_cycle_ratio;
  argint(0, &priority_ratio);
  argint(1, &arrival_time_ratio);
  argint(2, &executed_cycle_ratio);
  set_all_bjf_params(priority_ratio, arrival_time_ratio, executed_cycle_ratio);
}

void
sys_print_all_procs(void)
{
  print_all_procs();
}