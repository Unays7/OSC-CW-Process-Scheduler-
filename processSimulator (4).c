#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h> 
#include <semaphore.h>
#include <stdbool.h>

#include "coursework.h"
#include "linkedlist.h"

int PROCESS_COUNTER = 0;
int TERMINATED_COUNTER = 0;

// Referencing all of the functions 
void * process_generator();
void * long_term_scheduler();
void * short_term_scheduler(void *arg);
void * empty_pid_list(struct element *pHead, struct element *pTail);
void * termination_daemon();
void * booster_daemon();


void printHeadersSVG();
void printProcessSVG(int iCPUId, struct process * pProcess, struct timeval oStartTime, struct timeval oEndTime);
void printPrioritiesSVG();
void printRasterSVG();
void printFootersSVG();

// Intialising head and tail of Ready Queue(rq), NewQeue(nq) which has low priority & high priority and Termination Queue
struct element *eplHead = NULL;
struct element *eplTail = NULL;

struct element *nqHead = NULL;
struct element *nqTail = NULL;

struct element *priority_list_head[MAX_PRIORITY];
struct element *priority_list_tail[MAX_PRIORITY];

struct element *terminateHead = NULL;
struct element *terminateTail = NULL;

// Time structs 
struct timeval  oBaseTime;

//Creating the global process table
struct process *process_table[SIZE_OF_PROCESS_TABLE];

//Int to hold avergae RT and TT
long int average_turn_around_time = 0;
long int average_response_time = 0;
long int responseTime = 0;
long int turnaroundTime = 0;



//Creating the locking signals
pthread_cond_t free_pid;
pthread_cond_t process_available;
pthread_mutex_t mutex_FP;
pthread_mutex_t mutex_PG;
pthread_mutex_t mutex_SS[MAX_PRIORITY];
sem_t freePid;
sem_t processAvailable;


//Creating all of the thread strcutures
pthread_t process_thread;
pthread_t long_term_thread;
pthread_t short_term_thread[NUMBER_OF_CPUS];
pthread_t booster_daemon_thread;
pthread_t termination_daemon_thread;


int main(int argc, char* argv[])
{  

    /*
    printHeadersSVG();
    printPrioritiesSVG();
    printRasterSVG();
    */
    // Initialising Empty PID LinkedList
    for(int i = 0; i < SIZE_OF_PROCESS_TABLE; i++)
    {
        int *data = (int *) malloc(sizeof(int));
        *data = i;
        if (i == 0)
        {
            addFirst(data, &eplHead, &eplTail);
        }
        else
        {
            addLast(data, &eplHead, &eplTail);
        }
    
    }

    // Intilaising all mutex's for priority queue
    for(int i = 0; i < MAX_PRIORITY; i++)
    {
        pthread_mutex_init(&mutex_SS[i], NULL);
    }


    //Intialising the semaphores
    pthread_cond_init(&free_pid, NULL);
    pthread_cond_init(&process_available, NULL);
    pthread_mutex_init(&mutex_FP, NULL);
    pthread_mutex_init(&mutex_PG, NULL);

    
    

    //Intialising the creation of all of the the threads
    sem_init(&processAvailable, 0, 0);
    sem_init(&freePid, 0, 1);
    pthread_create(&process_thread, NULL, process_generator, NULL);
    sem_wait(&processAvailable);
    pthread_create(&long_term_thread, NULL, long_term_scheduler, NULL);
    sem_wait(&processAvailable);

     gettimeofday(&oBaseTime, NULL);

    //Creating a thread for every CPU
    for(int i = 0; i < NUMBER_OF_CPUS; i++)
    {
        int *x = malloc(sizeof(int));
        *x = i;
        pthread_create(&short_term_thread[i], NULL, short_term_scheduler, x);
    }
    
    pthread_create(&booster_daemon_thread, NULL, booster_daemon, NULL);
    pthread_create(&termination_daemon_thread, NULL, termination_daemon, NULL);

    for(int i = 0; i < NUMBER_OF_CPUS; i++)
    {
        void *o;
        pthread_join(short_term_thread[i], &o);
        free(o);
    }

    pthread_join(long_term_thread, NULL);
    pthread_join(termination_daemon_thread, NULL);
    pthread_detach(process_thread);
    pthread_join(booster_daemon_thread, NULL);

    for (int i = 0; i < MAX_PRIORITY; i++)
    {
        pthread_mutex_destroy(&mutex_SS[i]);
    }

    
    printFootersSVG();
    return 0;
}   

void * termination_daemon()
{
    printf("Entered termination!!!\n");

    while(TERMINATED_COUNTER < NUMBER_OF_PROCESSES)
    {
        //struct element *terminated_current = terminateHead;
        while(terminateHead != NULL)
        {
            //struct element *terminated_next = terminated_current->pNext;
            int *pid = removeFirst(&terminateHead, &terminateTail);
            if (pid == NULL)
            {
                break;
            }
            else
            {
                printf("TXT: Terminated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *pid, process_table[*pid]->iPriority, process_table[*pid]->iPreviousBurstTime, process_table[*pid]->iRemainingBurstTime);
                struct process *terminated_process = process_table[*pid];
                process_table[*pid] = NULL;
                free(terminated_process);
                addLast(pid, &eplHead, &eplTail); 
                TERMINATED_COUNTER++;
                //terminated_current = terminated_next;
                if (TERMINATED_COUNTER == NUMBER_OF_PROCESSES)
                {
                    printf("TXT: Average response time = %f, Average turnaround time = %f \n", (double) (average_response_time/NUMBER_OF_PROCESSES), (double) (average_turn_around_time/NUMBER_OF_PROCESSES));
                    return NULL;
                } 
                else
                {
                    sem_post(&freePid);
                }
                
            }
        }
        usleep(TERMINATION_INTERVAL * 1000);
    }
    return NULL;
}


void * booster_daemon()
{
    printf("Entered booster!!!\n");
    while(1)
    {
        int *boostPid = NULL;
        struct process *BoostedProcess; 
        while(TERMINATED_COUNTER < NUMBER_OF_PROCESSES)
        {
            for(int i = (MAX_PRIORITY/2) + 1; i < MAX_PRIORITY; i++)
            {
                if(TERMINATED_COUNTER == NUMBER_OF_PROCESSES)
                {
                    return NULL;
                }
                if(priority_list_head[i] != NULL)
                {
                    pthread_mutex_lock(&mutex_SS[i]);
                    boostPid = removeFirst(&priority_list_head[i], &priority_list_tail[i]);
                    pthread_mutex_unlock(&mutex_SS[i]);
                    if (boostPid == NULL)
                    {
                        break;
                    }
                    addLast(boostPid, &priority_list_head[(MAX_PRIORITY/2) - 1], &priority_list_tail[(MAX_PRIORITY/2) -1]);
                    printf("TXT: Boost: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *boostPid, process_table[*boostPid]->iPriority, process_table[*boostPid]->iPreviousBurstTime, process_table[*boostPid]->iRemainingBurstTime);
                }
            usleep(BOOST_INTERVAL);
            }   
        }

    }
    return NULL;
}

void * short_term_scheduler(void *arg)
{
    printf("Entered short term scheduler!!!\n");
    struct timeval  startTime;
    struct timeval   endTime; 
    int first_run = 0;
    int *runPid = NULL;
    int * current_running_CPU = (int *)arg; 
    while(1)
    {
        if (TERMINATED_COUNTER == NUMBER_OF_PROCESSES)
        {
            free(arg);
            return NULL;
        }
        for(int i = 0; i < MAX_PRIORITY; i++)
        {
            //struct element * prority_list_current = priority_list_head[i];
            while(priority_list_head[i] != NULL)
            {
                pthread_mutex_lock(&mutex_SS[i]);
                runPid = removeFirst(&priority_list_head[i], &priority_list_tail[i]);
                pthread_mutex_unlock(&mutex_SS[i]); 
                if(process_table[*runPid]->iPriority < (MAX_PRIORITY/2))
                {
                    runNonPreemptiveJob(process_table[*runPid], &startTime, &endTime);
                    responseTime = getDifferenceInMilliSeconds(process_table[*runPid]->oTimeCreated, process_table[*runPid]->oFirstTimeRunning);
                    turnaroundTime = getDifferenceInMilliSeconds(process_table[*runPid]->oFirstTimeRunning, process_table[*runPid]->oLastTimeRunning);
                    average_response_time += responseTime;
                    average_turn_around_time += turnaroundTime;
                    printf("TXT: Consumer %d, Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d, Turnaround Time = %d\n", *current_running_CPU + 1, *runPid, process_table[*runPid]->iPriority, process_table[*runPid]->iPreviousBurstTime, process_table[*runPid]->iRemainingBurstTime, responseTime, turnaroundTime);
                    /**
                    for(int x = 0; x < i; x++)
                    {
                        if(priority_list_head[x] != NULL)
                        {
                            preemptJob(process_table[*runPid]);
                            addLast(runPid, &priority_list_head[process_table[*runPid]->iPriority], &priority_list_tail[process_table[*runPid]->iPriority]);
                            i = x-1;
                        }
                    }
                    **/
                       
                    if(process_table[*runPid]->iRemainingBurstTime <= 0)
                    {
                        addLast(runPid, &terminateHead, &terminateTail);
                        i = 0;
                        continue;
                    }
                    else
                    {
                        addLast(runPid, &priority_list_head[process_table[*runPid]->iPriority], &priority_list_tail[process_table[*runPid]->iPriority]);
                    }
                    
                }
                else
                {
                    if(process_table[*runPid]->iInitialBurstTime == process_table[*runPid]->iRemainingBurstTime)
                    {
                        first_run = 1;
                    }
                    runPreemptiveJob(process_table[*runPid], &startTime, &endTime);

                    if (first_run == 1)
                    {
                        responseTime = getDifferenceInMilliSeconds(process_table[*runPid]->oTimeCreated, process_table[*runPid]->oFirstTimeRunning);
                        average_response_time += responseTime;
                        printProcessSVG(*current_running_CPU + 1, process_table[*runPid], startTime, endTime);
                        printf("TXT: Consumer %d, Process ID= %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d\n", *current_running_CPU + 1, *runPid, process_table[*runPid]->iPriority, process_table[*runPid]->iPreviousBurstTime, process_table[*runPid]->iRemainingBurstTime, responseTime);
                        addLast(runPid, &priority_list_head[process_table[*runPid]->iPriority], &priority_list_tail[process_table[*runPid]->iPriority]);
                    }

                    else if (process_table[*runPid]->iRemainingBurstTime <= 0)
                    {
                        turnaroundTime = getDifferenceInMilliSeconds(process_table[*runPid]->oFirstTimeRunning, process_table[*runPid]->oLastTimeRunning);
                        average_turn_around_time += turnaroundTime;
                        printf("TXT: Consumer %d, Process ID= %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Turnaround Time = %d\n", *current_running_CPU + 1, *runPid, process_table[*runPid]->iPriority, process_table[*runPid]->iPreviousBurstTime, process_table[*runPid]->iRemainingBurstTime, turnaroundTime);
                        printProcessSVG(*current_running_CPU + 1, process_table[*runPid], startTime, endTime);
                        first_run = 0;
                        addLast(runPid, &terminateHead, &terminateTail);
                        i = 0;
                        continue;
                    }
                    else
                    {
                        printf("TXT: Consumer %d, Process ID= %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *current_running_CPU + 1, *runPid, process_table[*runPid]->iPriority, process_table[*runPid]->iPreviousBurstTime, process_table[*runPid]->iRemainingBurstTime);
                        printProcessSVG(*current_running_CPU + 1, process_table[*runPid], startTime, endTime);
                        addLast(runPid, &priority_list_head[process_table[*runPid]->iPriority], &priority_list_tail[process_table[*runPid]->iPriority]);
                    }
                    first_run = 0;
                }
            }
        }
        //pthread_cond_wait(&process_available, &mutex_PG);
    }
    free(arg);
    return NULL;
}

void * long_term_scheduler()
{
    bool first_time = true;
    int long_counter = 0;
    //struct element * nqCurrent = nqHead; 
    printf("Entered Long Term Scheduler!!\n");
    while(1)
    {
        if (TERMINATED_COUNTER == NUMBER_OF_PROCESSES || long_counter == NUMBER_OF_PROCESSES)
        {
            return NULL;
        }
        while (nqHead != NULL)
        {
            if (TERMINATED_COUNTER == NUMBER_OF_PROCESSES || long_counter == NUMBER_OF_PROCESSES)
            {
                return NULL;
            }
            //struct element * nqNext = nqCurrent->pNext;
            int *pid = removeFirst(&nqHead, &nqTail);
            printf("TXT: Admitted: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *pid, process_table[*pid]->iPriority, process_table[*pid]->iPreviousBurstTime, process_table[*pid]->iRemainingBurstTime);
            addLast(pid, &priority_list_head[process_table[*pid]->iPriority], &priority_list_tail[process_table[*pid]->iPriority]);
            long_counter++;
            //nqCurrent = nqNext;
            if(first_time && long_counter == SIZE_OF_PROCESS_TABLE)
            {
                first_time = false;
                sem_post(&processAvailable);
            }
        }
    usleep(LONG_TERM_SCHEDULER_INTERVAL * 1000);
    }
    return NULL;
}


void * process_generator()
{
    // Check if any free pIDs
    // If so, make new processes
    // If not, wait until a process finishes

    int *pid;
    int process_counter = 0;
    bool first_time = true;
    printf("Entered Process Generator!!\n");
    //struct element *eplCurrent = eplHead;
    while(process_counter <= NUMBER_OF_PROCESSES)
    {
        sem_wait(&freePid);
        while(eplHead != NULL)
        {
            if (process_counter == NUMBER_OF_PROCESSES)
            {
                return NULL;
            }
            //struct element * eplNext = eplCurrent->pNext;
            pid = removeFirst(&eplHead, &eplTail);
            struct process *process = generateProcess(pid);
            process_counter++;
            process_table[*pid] = process;
            addLast(pid, &nqHead, &nqTail);
            printf("TXT: Generated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *pid, process_table[*pid]->iPriority, process_table[*pid]->iPreviousBurstTime, process_table[*pid]->iRemainingBurstTime);
            if (first_time)
            {
                first_time = false;
                sem_post(&processAvailable);
            }
            //eplCurrent = eplNext;
        }
    }

    return NULL;
}


void printHeadersSVG()
{
    printf("SVG: <!DOCTYPE html>\n");
    printf("SVG: <html>\n");
    printf("SVG: <body>\n");
    printf("SVG: <svg width=\"10000\" height=\"1100\">\n");
}

void printProcessSVG(int iCPUId, struct process *pProcess, struct timeval oStartTime, struct timeval oEndTime)
{
    int iXOffset = getDifferenceInMilliSeconds(oBaseTime, oStartTime) + 30;
    int iYOffsetPriority = (pProcess->iPriority + 1) * 16 - 12;
    int iYOffsetCPU = (iCPUId - 1) * (480 + 50);
    int iWidth = getDifferenceInMilliSeconds(oStartTime, oEndTime);
    printf("SVG: <rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"8\" style=\"fill:rgb(%d,0,%d);stroke-width:1;stroke:rgb(255,255,255)\"/>\n", iXOffset /* x */, iYOffsetCPU + iYOffsetPriority /* y */, iWidth, *(pProcess->pPID) - 1 /* rgb */, *(pProcess->pPID) - 1 /* rgb */);
}

void printPrioritiesSVG()
{
    for (int iCPU = 1; iCPU <= NUMBER_OF_CPUS; iCPU++)
    {
        for (int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
        {
            int iYOffsetPriority = (iPriority + 1) * 16 - 4;
            int iYOffsetCPU = (iCPU - 1) * (480 + 50);
            printf("SVG: <text x=\"0\" y=\"%d\" fill=\"black\">%d</text>\n", iYOffsetCPU + iYOffsetPriority, iPriority);
        }
    }
}

void printRasterSVG()
{
    for (int iCPU = 1; iCPU <= NUMBER_OF_CPUS; iCPU++)
    {
        for (int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
        {
            int iYOffsetPriority = (iPriority + 1) * 16 - 8;
            int iYOffsetCPU = (iCPU - 1) * (480 + 50);
            printf("SVG: <line x1=\"%d\" y1=\"%d\" x2=\"10000\" y2=\"%d\" style=\"stroke:rgb(125,125,125);stroke-width:1\"/>\n", 16, iYOffsetCPU + iYOffsetPriority, iYOffsetCPU + iYOffsetPriority);
        }
    }
}

void printFootersSVG()
{
    printf("SVG: Sorry, your browser does not support inline SVG.\n");
    printf("SVG: </svg>\n");
    printf("SVG: </body>\n");
    printf("SVG: </html>\n");
}