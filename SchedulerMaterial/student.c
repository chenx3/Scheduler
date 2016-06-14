/*
 * student.c
 * This file contains the CPU scheduler for the simulation.
 * original base code from http://www.cc.gatech.edu/~rama/CS2200
 * Last modified 5/11/2016 by Sherri Goings
 * Author: Tao Liu and Xi Chen
 */

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "os-sim.h"
#include "student.h"

// Local helper functions 
static void addReadyProcess(pcb_t* proc);
static pcb_t* getReadyProcess(void);
static void schedule(unsigned int cpu_id);
static void printReadyQueue();

/*
 * enum is useful C language construct to associate desriptive words with integer values
 * in this case the variable "alg" is created to be of the given enum type, which allows
 * statements like "if alg == FIFO { ...}", which is much better than "if alg == 1" where
 * you have to remember what algorithm is meant by "1"...
 * just including it here to introduce you to the idea if you haven't seen it before!
 */
typedef enum {
    FIFO = 0,
	RoundRobin,
	StaticPriority,
    MultiLevel
} scheduler_alg;

scheduler_alg alg;

// declare other global vars
int time_slice = -1;
int max_wait_time;
int cpu_count;


/*
 * main() parses command line arguments, initializes globals, and starts simulation
 */
int main(int argc, char *argv[])
{
    /* Parse command line args - must include num_cpus as first, rest optional
     * Default is to simulate using just FIFO on given num cpus, if 2nd arg given:
     * if -r, use round robin to schedule (must be 3rd arg of time_slice)
     * if -p, use static priority to schedule
     */
    if (argc == 2) {
		alg = FIFO;
		printf("running with basic FIFO\n");
	}
	else if(argc > 4 && strcmp(argv[2],"-m") == 0){
 		alg = MultiLevel;
		time_slice = atoi(argv[3]);
        max_wait_time = atoi(argv[4]);
		printf("running with multi level, time slice = %d, max wait time = %d\n", time_slice,max_wait_time);
    }
    else if (argc > 2 && strcmp(argv[2],"-r")==0 && argc > 3) {
		alg = RoundRobin;
		time_slice = atoi(argv[3]);
		printf("running with round robin, time slice = %d\n", time_slice);
	}
	else if (argc > 2 && strcmp(argv[2],"-p")==0) {
		alg = StaticPriority;
		printf("running with static priority\n");
	}
	else {
        fprintf(stderr, "Usage: ./os-sim <# CPUs> [ -r <time slice>| -p | -m <time slice> <max time>]\n"
            "    Default : FIFO Scheduler\n"
            "         -r : Round-Robin Scheduler (must also give time slice)\n"
            "         -m : MultiLeve Scheduler (must also give time slice and max wait time)\n"
            "         -p : Static Priority Scheduler\n\n");
        return -1;
    }
	fflush(stdout);

    /* atoi converts string to integer */
    cpu_count = atoi(argv[1]);

    // initialize the four priority queues
    queues = malloc(sizeof(pcb_t**) * 4);
    queue0 = malloc(sizeof(pcb_t*) * 2);
    queue1 = malloc(sizeof(pcb_t*) * 2);
    queue2 = malloc(sizeof(pcb_t*) * 2);
    queue3 = malloc(sizeof(pcb_t*) * 2);
    int i;
    for (i=0; i<2; i++) {
        queue0[i] = NULL;
    }
    for (i=0; i<2; i++) {
        queue1[i] = NULL;
    }
    for (i=0; i<2; i++) {
        queue2[i] = NULL;
    }
    for (i=0; i<2; i++) {
        queue3[i] = NULL;
    }
    queues[0] = queue0;
    queues[1] = queue1;
    queues[2] = queue2;
    queues[3] = queue3;

    /* Allocate the current[] array and its mutex */
    current = malloc(sizeof(pcb_t*) * cpu_count);
    for (i=0; i<cpu_count; i++) {
        current[i] = NULL;
    }
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);

    /* Initialize other necessary synch constructs */
    pthread_mutex_init(&ready_mutex, NULL);
    pthread_cond_init(&ready_empty, NULL);

    /* Start the simulator in the library */
    printf("starting simulator\n");
    fflush(stdout);
    start_simulator(cpu_count);


    return 0;
}

/*
 * idle() is called by the simulator when the idle process is scheduled.
 * It blocks until a process is added to the ready queue, and then calls
 * schedule() to select the next process to run on the CPU.
 *
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void idle(unsigned int cpu_id)
{
  pthread_mutex_lock(&ready_mutex);
  if(alg == MultiLevel){
    while (queues[0][0] == NULL && queues[1][0] == NULL && queues[2][0] == NULL &&queues[3][0] == NULL) {
      pthread_cond_wait(&ready_empty, &ready_mutex);
    }
  }else{
    while (head == NULL) {
      pthread_cond_wait(&ready_empty, &ready_mutex);
    }
  }
  pthread_mutex_unlock(&ready_mutex);
  schedule(cpu_id);
}

/*
 * schedule() is your CPU scheduler. It currently implements basic FIFO scheduling -
 * 1. calls getReadyProcess to select and remove a runnable process from your ready queue
 * 2. updates the current array to show this process (or NULL if there was none) as
 *    running on the given cpu
 * 3. sets this process state to running (unless its the NULL process)
 * 4. calls context_switch to actually start the chosen process on the given cpu
 *    - note if proc==NULL the idle process will be run
 *    - note the final arg of -1 means there is no clock interrupt
 *	context_switch() is prototyped in os-sim.h. Look there for more information.
 *  a basic getReadyProcess() is implemented below, look at the comments for info.
 *
 * TO-DO: handle scheduling with a time-slice when necessary
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
static void schedule(unsigned int cpu_id) {
  if (alg == MultiLevel){
    checkMaxTime();
    pcb_t* proc = getProcessfromQueues();
    pthread_mutex_lock(&current_mutex);
    current[cpu_id] = proc;
    pthread_mutex_unlock(&current_mutex);
    if (proc!=NULL) {
      proc->state = PROCESS_RUNNING;
    }
    context_switch(cpu_id, proc, time_slice);
  }else{
    pcb_t* proc = getReadyProcess();
    pthread_mutex_lock(&current_mutex);
    current[cpu_id] = proc;
    pthread_mutex_unlock(&current_mutex);

    if (proc!=NULL) {
      proc->state = PROCESS_RUNNING;
    }
    if (alg == RoundRobin){
      context_switch(cpu_id, proc, time_slice);
    }else{
      context_switch(cpu_id, proc, -1);
    }
  }
}


/*
 * preempt() is called when a process is preempted due to its timeslice expiring.
 *
 * This function should place the currently running process back in the
 * ready queue, then call schedule() to select a new runnable process.
 *
 * THIS FUNCTION MUST BE IMPLEMENTED FOR ROUND ROBIN OR PRIORITY SCHEDULING
 */
extern void preempt(unsigned int cpu_id) {
  if (alg == MultiLevel){
    current[cpu_id]->priority = current[cpu_id]->priority + 1;
    if (current[cpu_id]->priority > 3){
      current[cpu_id]->priority = 3;
    }
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_READY;
    addToQueue(current[cpu_id]->priority, current[cpu_id]);
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
  }else{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_READY;
    addReadyProcess(current[cpu_id]);
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
  }
}


/*
 * yield() is called by the simulator when a process performs an I/O request
 * note this is different than the concept of yield in user-level threads!
 * In this context, yield sets the state of the process to waiting (on I/O),
 * then calls schedule() to select a new process to run on this CPU.
 * args: int - id of CPU process wishing to yield is currently running on.
 *
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void yield(unsigned int cpu_id) {
  if (alg == MultiLevel){
    current[cpu_id]->priority = current[cpu_id]->priority - 1;
  }
  //use lock to ensure thread-safe access to current process
  pthread_mutex_lock(&current_mutex);
  current[cpu_id]->state = PROCESS_WAITING;
  pthread_mutex_unlock(&current_mutex);
  schedule(cpu_id);

}


/*
 * terminate() is called by the simulator when a process completes.
 * marks the process as terminated, then calls schedule() to select
 * a new process to run on this CPU.
 * args: int - id of CPU process wishing to terminate is currently running on.
 *
 * THIS FUNCTION IS ALREADY COMPLETED - DO NOT MODIFY
 */
extern void terminate(unsigned int cpu_id) {
    // use lock to ensure thread-safe access to current process
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_TERMINATED;
    current[cpu_id]->priority = -1;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

/*
 * wake_up() is called for a new process and when an I/O request completes.
 * The current implementation handles basic FIFO scheduling by simply
 * marking the process as READY, and calling addReadyProcess to put it in the
 * ready queue.  No locks are needed to set the process state as its not possible
 * for anyone else to also access it at the same time as wake_up
 *
 * TO-DO: If the scheduling algorithm is static priority, wake_up() may need
 * to preempt the CPU with the lowest priority process to allow it to
 * execute the process which just woke up.  However, if any CPU is
 * currently running idle, or all of the CPUs are running processes
 * with a higher priority than the one which just woke up, wake_up()
 * should not preempt any CPUs. To preempt a process, use force_preempt().
 * Look in os-sim.h for its prototype and parameters.
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
extern void wake_up(pcb_t *process) {
  if (alg == MultiLevel){
    process->state = PROCESS_READY;
    int priority = process->priority;
    if (priority < 0){
      priority = 0;
    }
    if (priority > 3){
      priority = 3;
    }
    addToQueue(priority, process);
  }else{
    if(alg != StaticPriority){
      process->state = PROCESS_READY;
      addReadyProcess(process);
    }else{
      // change process's state to ready and add it to the ready queue
      process->state = PROCESS_READY;
      addReadyProcess(process);
      // check if all cpus are full
      int full = 1;
      int i;
      for (i=0; i<cpu_count; i++) {
        if (current[i] == NULL){
          full = 0;
        }
      }
      // if all cpus are full, find the smallest priority in cpus' processes
      int cpuid;
      int smallest = 11;
      if (full == 1){
        for (i=0; i<cpu_count; i++) {
          if(current[i]->static_priority < smallest){
            cpuid = i;
            smallest = current[i]->static_priority;
          }
        }
      }
      //if the process's priority is greater than the process with smallest priority, preemt the process
      if (process->static_priority > smallest && full == 1){
        current[cpuid]->state = PROCESS_READY;
        addReadyProcess(current[cpuid]);
        force_preempt(cpuid);
      }
    }
  }
}


/* The following 2 functions implement a FIFO ready queue of processes */

/*
 * addReadyProcess adds a process to the end of a pseudo linked list (each process
 * struct contains a pointer next that you can use to chain them together)
 * it takes a pointer to a process as an argument and has no return
 */
static void addReadyProcess(pcb_t* proc) {

  // ensure no other process can access ready list while we update it
  pthread_mutex_lock(&ready_mutex);

  // add this process to the end of the ready list
  if (head == NULL) {
    head = proc;
    tail = proc;
    // if list was empty may need to wake up idle process
    pthread_cond_signal(&ready_empty);
  }
  else {
    tail->next = proc;
    tail = proc;
  }

  // ensure that this proc points to NULL
  proc->next = NULL;

  pthread_mutex_unlock(&ready_mutex);
}


/*
 * getReadyProcess removes a process from the front of a pseudo linked list (each process
 * struct contains a pointer next that you can use to chain them together)
 * it takes no arguments and returns the first process in the ready queue, or NULL
 * if the ready queue is empty
 *
 * TO-DO: handle priority scheduling
 *
 * THIS FUNCTION IS PARTIALLY COMPLETED - REQUIRES MODIFICATION
 */
static pcb_t* getReadyProcess(void) {
  if(alg != StaticPriority){
    // ensure no other process can access ready list while we update it
    pthread_mutex_lock(&ready_mutex);

    // if list is empty, unlock and return null
    if (head == NULL) {
	  pthread_mutex_unlock(&ready_mutex);
	  return NULL;
    }

    // get first process to return and update head to point to next process
    pcb_t* first = head;
    head = first->next;

    // if there was no next process, list is now empty, set tail to NULL
    if (head == NULL) tail = NULL;

    pthread_mutex_unlock(&ready_mutex);
    return first;


  }else{
    // ensure no other process can access ready list while we update it
    pthread_mutex_lock(&ready_mutex);

    // if list is empty, unlock and return null
    if (head == NULL) {
	  pthread_mutex_unlock(&ready_mutex);
	  return NULL;
    }

    // find the process with the highest priority
    pcb_t* node = head;
    pcb_t* highest = head;
    while(node->next != NULL){
      if (node->static_priority > highest->static_priority){
        highest = node;
      }
      node = node->next;
    }

    // delete the process with the highest priority from the ready queue
    if (highest->pid == head->pid){
      head = highest->next;
    }else{
      node = head;
      while (node->next != NULL){
        if (node->next->pid == highest->pid){
          node->next = highest->next;
          if(highest == tail){
            tail = node;
          }
        }
        node = node->next;
      }
    }
    // if there was no next process, list is now empty, set tail to NULL
    if (head == NULL) tail = NULL;
    pthread_mutex_unlock(&ready_mutex);
    return highest;
  }
}

// add a process to the queue indicated by the parameter i
static void addToQueue(int i, pcb_t* proc){
  proc->wait_time = simulator_time;
  // ensure no other process can access ready list while we update it
  pthread_mutex_lock(&ready_mutex);
  if (i < 0) {
    i = 0;
    proc->priority = 0;
  } else if (i > 3) {
    i = 3;
    proc->priority = 3;
  }
  // add this process to the end of the ready list
  if (queues[i][0] == NULL) {
    queues[i][0] = proc;
    queues[i][1] = proc;
    // if list was empty may need to wake up idle process
    pthread_cond_signal(&ready_empty);
  }
  else {
    queues[i][1]->next = proc;
    queues[i][1] = proc;
  }
  // ensure that this proc points to NULL
  proc->next = NULL;
  pthread_mutex_unlock(&ready_mutex);
}

// get one process according to its priority
static pcb_t* getProcessfromQueues(void){
  int j;
  pthread_mutex_lock(&ready_mutex);
  for (j=0; j<4; j++) {
    if(queues[j][0] != NULL){
      // get first process to return and update head to point to next process
      pcb_t* first = queues[j][0];
      queues[j][0] = first->next;

      // if there was no next process, list is now empty, set tail to NULL
      if (queues[j][0] == NULL) queues[j][1] = NULL;
      pthread_mutex_unlock(&ready_mutex);
      return first;
    }
  }
  pthread_mutex_unlock(&ready_mutex);
  return NULL;
}

// check whether there is any process waits more than max wait time
static void checkMaxTime(){
  int j;
  pcb_t* node;
  pcb_t* current;
  for (j=0; j<4; j++) {
    node = queues[j][0];
    if(node != NULL){
      while(node != NULL){
        current = node;
        node = node->next;
        if (simulator_time - current->wait_time > max_wait_time){
          removeFromQueue(current->priority, current);
          current->priority = current->priority - 1;
          addToQueue(current->priority, current);
        }
      }
    }
  }
}

// remove a process from a queue
static void removeFromQueue(int i, pcb_t* proc){
  pthread_mutex_lock(&ready_mutex);
  if (i < 0) {
    i = 0;
    proc->priority = 0;
  } else if (i > 3) {
    i = 3;
    proc->priority = 3;
  }
  pcb_t* node = queues[i][0];
  // delete the process with the highest priority from the ready queue
  if (proc->pid == queues[i][0]->pid){
    queues[i][0] = proc->next;

  }else{
    node = queues[i][0];
    while (node->next != NULL){
      if (node->next->pid == proc->pid){
        node->next = proc->next;
        if(proc == queues[i][1]){
          queues[i][1] = node;
        }
      }
      node = node->next;
    }
  }
  // if there was no next process, list is now empty, set tail to NULL
  if (queues[i][0] == NULL) queues[i][1] = NULL;
  pthread_mutex_unlock(&ready_mutex);
}

static void printReadyQueue(){
  int j;
  pcb_t* node;
  pcb_t* current;
  for (j=0; j<4; j++) {
    node = queues[j][0];
    if(node != NULL){
      while(node != NULL){
        printf("queue %d process id %d\n", j, node->pid);
        node = node->next;

      }
    }
  }
}
