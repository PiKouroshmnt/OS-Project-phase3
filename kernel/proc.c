#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->usage.sum_of_ticks = 0;
      p->usage.quota = 0;
      p->state = UNUSED;
      p->current_thread = 0;
      p->last_scheduled_index = 0;
      for(int i = 0;i < MAX_THREAD;i++) {
          p->threads[i].state = THREAD_FREE;
      }
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->current_thread = 0;
  p->usage.sum_of_ticks = 0;
  p->usage.quota = 0;
  for(int i = 0;i < MAX_THREAD;i++) {
      p->threads[i].state = THREAD_FREE;
  }
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
//    internal_report_list.numberOfReports = 0;
//    internal_report_list.writeIndex = 0;
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->usage.sum_of_ticks = 0;
  p->usage.quota = 0;
  if(p->current_thread)
    p->current_thread->state = THREAD_JOINED;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct thread *t = 0;
  int flag = 0;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;

        //if process has created threads...
        if(p->current_thread != 0) {
            for(int i = 0; i < MAX_THREAD;i++) {
                //find first ready/runnable thread
                int index = (p->last_scheduled_index + 1 + i) % MAX_THREAD;
                t = &p->threads[index];
                if (t->state == THREAD_RUNNABLE && t->join == 0) {
                    p->current_thread = t;
                    p->last_scheduled_index = index;
                    break;
                }
            }
            if(t && t->state == THREAD_RUNNABLE) {
                t->state = THREAD_RUNNING;
            }else {
                panic("this shouldn't happen but just in case");
            }

            memmove(p->trapframe,t->trapframe,sizeof (struct trapframe));

            //this is debug only serves no purpose
            //printf("\n");
        }
        p->usage.start_tick = ticks;
        swtch(&c->context, &p->context);

        if(p->current_thread && p->current_thread->state == THREAD_RUNNABLE){
            t = 0;
            memmove(p->current_thread->trapframe,p->trapframe,sizeof (struct trapframe));

        }
        if(p->state == TEXIT) {
            flag = 1;
            p->state = RUNNABLE;
        }

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
      if(flag) {
          p--;
          flag = 0;
      }
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING || (p->current_thread && p->current_thread->state == THREAD_RUNNING))
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  uint elapsed = ticks - p->usage.start_tick;
  p->usage.sum_of_ticks += elapsed;

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  if(p->current_thread && p->current_thread->state == THREAD_RUNNING) {
      p->current_thread->state = THREAD_RUNNABLE;
  }
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

void
find_children(struct child_processes *chp , struct proc *parent)
{
    struct proc *p;

    for(p = proc; p < &proc[NPROC];p++){
        acquire(&p->lock);
        if (p->parent == parent){
            strncpy(chp->processes[chp->count].name,p->name,16);
            chp->processes[chp->count].pid = p->pid;
            chp->processes[chp->count].ppid = parent->pid;
            chp->processes[chp->count].state = p->state;
            chp->count++;
            release(&p->lock);

            find_children(chp,p);
        } else {
            release(&p->lock);
        }
    }
}

int
get_reports(struct report_traps *rprt)
{
    struct proc *p = myproc();
    struct child_processes *chp = kalloc();
    memset(chp,0,sizeof(struct child_processes));
    find_children(chp,p);
    for(int i = 0; i < internal_report_list.numberOfReports; i++) {
        if (internal_report_list.reports[i].pid == p->pid) {
            rprt->reports[rprt->count].pid = p->pid;
            rprt->reports[rprt->count].stval = internal_report_list.reports[i].stval;
            rprt->reports[rprt->count].sepc = internal_report_list.reports[i].sepc;
            rprt->reports[rprt->count].scause = internal_report_list.reports[i].scause;
            strncpy(rprt->reports[rprt->count].pname, internal_report_list.reports[i].pname, 16);
            rprt->count++;
        }
    }
    for(int j = 0;j < chp->count;j++) {
        int pid = chp->processes[j].pid;
        for(int i = 0; i < internal_report_list.numberOfReports; i++) {
            if (internal_report_list.reports[i].pid == pid) {
                rprt->reports[rprt->count].pid = pid;
                rprt->reports[rprt->count].stval = internal_report_list.reports[i].stval;
                rprt->reports[rprt->count].sepc = internal_report_list.reports[i].sepc;
                rprt->reports[rprt->count].scause = internal_report_list.reports[i].scause;
                strncpy(rprt->reports[rprt->count].pname, internal_report_list.reports[i].pname, 16);
                rprt->count++;
            }
        }
    }
    return 0;
}

void
thread_exit(void) {
    struct proc *p = myproc();
    struct thread *t = p->current_thread;

    acquire(&p->lock);

    t->state = THREAD_JOINED;

    for(int i = 0; i < MAX_THREAD; i++) {
        if(p->threads[i].id != t->id && p->threads[i].join == t->id) {
            p->threads[i].join = 0;
        }
    }

    if (t->trapframe) {
        uvmdealloc(p->pagetable, (uint64)t->trapframe->sp, (uint64)(t->trapframe->sp - STACKSIZE));
        p->sz -= STACKSIZE;
        kfree((void *)t->trapframe);
        t->trapframe = 0;
    }

    int threads_active = (p->threads[0].state == THREAD_JOINED) ? 0 : 1;

    if (!threads_active) {
        p->state = ZOMBIE;
        release(&p->lock);
        wakeup(p->parent);
    }

    p->state = TEXIT;
    sched();
    release(&p->lock);
}

int
thread_create(void* (*func)(void *) , void *arg)
{
    struct thread *t = 0;
    struct proc *p = myproc();
    uint64 stack_top;
    uint nexttid = 0;

    if (p->current_thread == 0) {
        p->current_thread = &p->threads[0];
        struct thread *main_thread = p->current_thread;

        main_thread->state = THREAD_RUNNABLE;
        main_thread->id = 0;
        main_thread->join = 0;
        main_thread->trapframe = (struct trapframe *)kalloc();
        if (!main_thread->trapframe) {
            intr_on();
            panic("trapframe alloc failed: main");
            return -1;
        }

        memmove(main_thread->trapframe, p->trapframe, sizeof(struct trapframe));
        main_thread->trapframe->sp = p->trapframe->sp;
    }

    for(int i = 0 ;i < MAX_THREAD; i++) {
        if (p->threads[i].state == THREAD_FREE) {
            nexttid = p->threads[i-1].id + 1;
            t = &p->threads[i];
            break;
        }
    }

    if(t == 0) {
        intr_on();
        panic("max threads reached");
        return -1;
    }

    stack_top = uvmalloc(p->pagetable, p->sz,p->sz + STACKSIZE, PTE_U | PTE_W);
    if (stack_top == 0) {
        intr_on();
        panic("stack alloc failed");
        return -1;
    }
    p->sz += STACKSIZE;

    t->join = nexttid;
    t->state = THREAD_RUNNABLE;
    t->id = nexttid;


    t->trapframe = (struct trapframe *) kalloc();
    if(!t->trapframe) {
        intr_on();
        panic("trapframe alloc failed: t");
        return -1;
    }

    memmove(t->trapframe, p->trapframe, sizeof (struct trapframe));
    t->trapframe->epc = (uint64)func;
    t->trapframe->a0 = (uint64)arg;
    t->trapframe->sp = stack_top;
    t->trapframe->ra = (uint64)-1;

    t->join = 0;

    return t->id;
}

int
thread_join(int tid)
{
    struct proc *p = myproc();
    struct thread *t = 0;

    acquire(&p->lock);

    // find thread with thread_id
    for(int i = 0; i < MAX_THREAD; i++) {
        if(p->threads[i].id == tid){
            t = &p->threads[i];
            break;
        }
    }

    // there isn't any thread with this id
    if(t == 0) {
        release(&p->lock);
        return -1;
    }

    if(t->state == THREAD_FREE || t->state == THREAD_JOINED) {
        release(&p->lock);
        return 0;
    }

    p->current_thread->join = tid;
    p->current_thread->state = THREAD_RUNNABLE;
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
    return 0;
}

int
thread_stop(int tid){
    struct proc *p = myproc();
    struct thread *t = p->current_thread;

    if(tid == -1) {
        thread_exit();
        return 0;
    }

    // find thread whit thread_id
    for(int i = 0; i < MAX_THREAD; i++) {
        if(p->threads[i].id == tid){
            t = &p->threads[i];
            break;
        }
    }

    if (t == 0) {
        return -1;
    }

    p->current_thread->state = THREAD_RUNNABLE;
    memmove(p->current_thread->trapframe,p->trapframe,sizeof (struct trapframe));
    p->current_thread = t;
    printf("%d exit\n",t->id);
    thread_exit();
    return 0;
}