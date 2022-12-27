#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define STARVING_THRESHOLD 8000
#define MIN_BJF_RANK 1000000
#define DEFAULT_MAX_TICKETS 30

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

int
generate_random_number(int min, int max)
{
    if (min >= max)
        return max > 0 ? max : -1 * max;
    acquire(&tickslock);
    int rand_num, diff = max - min + 1, time = ticks;
    release(&tickslock);
    rand_num = (1 + (1 + ((time + 2) % diff ) * (time + 1) * 132) % diff) * (1 + time % max) * (1 + 2 * max % diff);
    rand_num = rand_num % (max - min + 1) + min;
    return rand_num;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->entered_queue = ticks;
  p->queue = 2;
  p->executed_cycle = 0;
  p->priority_ratio = 1;
  p->arrival_time_ratio = 1;
  p->executed_cycle_ratio = 1;
  p->priority = 1;
  p->tickets = generate_random_number(1, DEFAULT_MAX_TICKETS);

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

void
fix_queues(void) {
    struct proc *p;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state == RUNNABLE)
            if (ticks - p->entered_queue >= STARVING_THRESHOLD) {
                p->queue = 1;
                p->entered_queue = ticks;
            }
    }
}


struct proc* round_robin(void) { // for queue 1 with the highest priority
    struct proc *p;
    struct proc *min_p = 0;
    int time = ticks;
    int starvation_time = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state != RUNNABLE || p->queue != 1)
            continue;
            
        if (p->state != RUNNABLE || p->queue != 1)
            continue;

        int starved_for = time - p->entered_queue;
        if (starved_for > starvation_time) {
            starvation_time = starved_for;
            min_p = p;
        }
    }
    return min_p;
}

float
get_rank(struct proc* p)
{
  return 
    p->priority * p->priority_ratio
    + p->entered_queue * p->arrival_time_ratio
    + p->executed_cycle * p->executed_cycle_ratio;
}

struct proc*
bjf(void)
{
  struct proc* p;
  struct proc* min_p = 0;
  float min_rank = MIN_BJF_RANK;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ 
    if (p->state != RUNNABLE || p->queue != 3)
      continue;
    if (get_rank(p) < min_rank){
      min_p = p;
      min_rank = get_rank(p);
    }
  }

  return min_p;
}

struct proc* lottery(void) { // for queue #2 and entrance queue
    struct proc *p;
    int total_tickets = 0;
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state != RUNNABLE || p->queue != 2)
            continue;
        total_tickets += p->tickets;
    }
    int winning_ticket = generate_random_number(1, total_tickets);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->state != RUNNABLE || p->queue != 2)
            continue;
        winning_ticket -= p->tickets;
        if (winning_ticket <= 0)
            return p;
    }
    return 0;
}

void
scheduler(void) {
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;

    for (;;) {
        // Enable interrupts on this processor.
        sti();

        // Loop over process table looking for process to run.
        acquire(&ptable.lock);
        fix_queues();
        p = round_robin();
        if (p == 0)
            p = lottery();
        if (p == 0)
            p = bjf();
        if (p == 0) {
            release(&ptable.lock);
            continue;
        }
        p->entered_queue = ticks;

        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;

        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        release(&ptable.lock);
    }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->executed_cycle += 0.1;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan) {
    struct proc *p;

    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// return parents's pid
int 
get_parent_pid() {
  struct proc *p = myproc()->parent;
  return p->pid;
}

// find the largest prime factor of a number
int
find_largest_prime_factor(int n)
{
    int maxPrime = -1;

    while (n % 2 == 0) {
        maxPrime = 2;
        n = n / 2;
    }
    while (n % 3 == 0) {
        maxPrime = 3;
        n = n / 3;
    }

    for (int i = 5; i <= n; i += 6) {
        while (n % i == 0) {
            maxPrime = i;
            n = n / i;
        }
        while (n % (i + 2) == 0) {
            maxPrime = i + 2;
            n = n / (i + 2);
        }
    }

    if (n > 4)
        maxPrime = n;

    return maxPrime;
}


int syscalls_pid_stack[NUM_OF_SYSCALLS][MAX_PID_NUM_SAVED];
int syscalls_order_pid_stack[NUM_OF_SYSCALLS][MAX_PID_NUM_SAVED];
int pid_num_of_calls[MAX_PID_TRACED];

void
push_pid_in_stack(int pid,int num)
{
    int i;
    for (i = 0; syscalls_pid_stack[num][i]!=0; i++) {}
    syscalls_pid_stack[num][i] = pid;
    syscalls_order_pid_stack[num][i] = ++pid_num_of_calls[pid];
}


// find the pid callers of a syscall and order that pids called that syscall
void
get_callers(int syscall_number)
{
    if(syscalls_pid_stack[syscall_number][0] == 0) {
      cprintf("this syscall is not called yet!!!\n");
      return;
    }

    cprintf("pid: order that pid called this syscall\n");
    for (int i = 0; syscalls_pid_stack[syscall_number][i] != 0; i++) {

      if(i%MAX_PID_OUTPUT_IN_ONE_LINE == 0) cprintf("\n");
      
      if (syscalls_pid_stack[syscall_number][i+1] != 0)
        cprintf("%d:%d ,", syscalls_pid_stack[syscall_number][i],
                           syscalls_order_pid_stack[syscall_number][i]);
      else
        cprintf("%d:%d\n", syscalls_pid_stack[syscall_number][i],
                           syscalls_order_pid_stack[syscall_number][i]);
    }
    
}

void
set_proc_queue(int pid, int queue)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
      p->queue = queue;
  }
  release(&ptable.lock);
}

void
set_lottery_params(int pid, int ticket_chance){
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
      p->tickets = ticket_chance;
  }
  release(&ptable.lock); 
}

void
set_a_proc_bjf_params(int pid, int priority_ratio, int arrival_time_ratio, int executed_cycle_ratio)
{
  struct proc *p;

  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == pid)
    {
      p->priority_ratio = priority_ratio;
      p->arrival_time_ratio = arrival_time_ratio;
      p->executed_cycle_ratio = executed_cycle_ratio; 
    }
  }
  release(&ptable.lock); 
}

void
set_all_bjf_params(int priority_ratio, int arrival_time_ratio, int executed_cycle_ratio) {
    struct proc *p;

    acquire(&ptable.lock);
    for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        p->priority_ratio = priority_ratio;
        p->arrival_time_ratio = arrival_time_ratio;
        p->executed_cycle_ratio = executed_cycle_ratio;
    }
    release(&ptable.lock);
}


void
printfloat(float num){
  int beg=(int)(num);
	int fin=(int)(num*100)-beg*100;
  cprintf("%d", beg);
  cprintf(".");
	if(fin<10)
    cprintf("0");
	cprintf("%d", fin);
}
int 
get_lenght(int num)
{
  int len = 0;
  if(num == 0)
    return 1;
  while(num > 0){
    num /= 10;
    len++;
  }
  return len;
}
void 
print_all_procs()
{
    struct proc *p;
    cprintf("name       pid       state       queue       arrival_time        tickets     p_ratio      e_ratio       a_ratio       rank       exec_cycle\n");
    cprintf("...........................................................................................................................................\n");
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){ 
      if (p->state == UNUSED)
        continue;


    cprintf(p->name);
    for(int i = 0; i < 11 - strlen(p->name); i++) cprintf(" ");

    cprintf("%d", p->pid);
    for(int i = 0; i < 10 - get_lenght(p->pid); i++) cprintf(" ");  

    char* state;
    if (p->state == 0)
      state="UNUSED";
    else if (p->state == 1)
      state="EMBRYO";
    else if (p->state == 2)
      state="SLEEPING";
    else if (p->state == 3)
      state="RUNNABLE";
    else if (p->state == 4)
      state="RUNNING";
    else if (p->state == 5)
      state="ZOMBIE";
    cprintf(state);
    for(int i = 0; i < 12 - strlen(state); i++) cprintf(" ");

    char* queue;
    if (p->queue == 1)
      queue="RoundRobin";
    else if (p->queue == 2)
      queue="Lottery";
    else if (p->queue == 3)
      queue="BJF";
    cprintf(queue);
    for(int i = 0; i < 12 - strlen(queue); i++) cprintf(" ");  

    cprintf("%d", p->entered_queue);
    for(int i = 0; i < 20 - get_lenght(p->entered_queue); i++) cprintf(" ");  

    cprintf("%d", p->tickets);
    for(int i = 0; i < 12 - get_lenght(p->tickets) - 1; i++) cprintf(" ");

    cprintf("%d", p->priority_ratio);
    for(int i = 0; i < 13 - get_lenght(p->priority_ratio); i++) cprintf(" ");

    cprintf("%d", p->executed_cycle_ratio);
    for(int i = 0; i < 14 - get_lenght(p->executed_cycle_ratio); i++) cprintf(" ");

    cprintf("%d", p->arrival_time_ratio);
    for(int i = 0; i < 14 - get_lenght(p->arrival_time_ratio); i++) cprintf(" ");      

    printfloat(get_rank(p));
    for(int i = 0; i < 11 - get_lenght((int)get_rank(p))-2; i++) cprintf(" ");  

    float executed_cycle = p->executed_cycle*10;
    if(executed_cycle - (int)(executed_cycle) <= 0.5)
      cprintf("%d", (int)(executed_cycle));
    else
      cprintf("%d", (int)(executed_cycle)+1);

    cprintf("\n");
  }
  release(&ptable.lock);
  
}