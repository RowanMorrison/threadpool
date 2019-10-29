#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

#include "queue.h"

#include "threadpool.h"

typedef struct __pool *p_pool;

typedef struct __thread {
	p_pool pool;
	queue task_queue;
	HANDLE event_on_data;
} t_thread, *p_thread;

typedef struct __pool {
	p_thread* threads;
	CRITICAL_SECTION rw_mutex;  

	HANDLE event_on_state;
	int threads_num;

	volatile int threads_alive;
	volatile int threads_working;
	volatile int keep_alive;
} t_pool;

typedef struct __task {
	void (*on_task)(void *);
    void *args; 
} t_task, *p_task;


    /*  Prototypes  */

p_pool pool_create(int n);
void pool_destroy(p_pool);
int pool_add_task(p_pool, void (*on_task)(void *), void*args);
void pool_wait(p_pool);

static p_thread thread_create(p_pool);
static unsigned long int WINAPI thread_loop(void *);
static void thread_destroy(p_thread thread);

static p_task task_create(void (*task)(void *), void *args);
static void task_destroy(p_task task);

    /*  Pool functions  */

p_pool pool_create(int n)
{
    p_pool pool = malloc(sizeof(t_pool));
    if (pool == NULL)
        return NULL;

    pool->threads = malloc(n * sizeof(void *));
    if (pool->threads == NULL) {
        free(pool);
        return NULL;
    }

	InitializeCriticalSection(&pool->rw_mutex);

	// is used for signalling about the internal state of a pool
	pool->event_on_state = CreateEventA(
		NULL, 
		FALSE, // auto reset
		FALSE, // non-signaled
		NULL);
	
	pool->threads_alive = 0;
	pool->threads_working = 0;
	pool->threads_num = n;

	pool->keep_alive = 1;

    for (int i = 0; i < n; i++)
        pool->threads[i] = thread_create(pool);

	// wait until all threads are running
	WaitForSingleObject(pool->event_on_state, INFINITE);

    return pool;
}


int pool_add_task(p_pool pool, void (*on_task)(void *), void *args)
{
	EnterCriticalSection(&pool->rw_mutex);
	if (pool->keep_alive == 0) {
		LeaveCriticalSection(&pool->rw_mutex);
		return -1;
	}
	// round-robin to distribute tasks
	static int i = 0;

	p_task task = task_create(on_task, args);
	if (task == NULL) {
		LeaveCriticalSection(&pool->rw_mutex);
		return -1;
	}

	q_enque(pool->threads[i]->task_queue, (void *)task);
	SetEvent(pool->threads[i]->event_on_data);
	i = (i + 1) % pool->threads_num;

	LeaveCriticalSection(&pool->rw_mutex);
	return 0;
}

void pool_wait(p_pool pool)
{
	WaitForSingleObject(pool->event_on_state, INFINITE);
}

void pool_destroy(p_pool pool)
{
	if (pool == NULL)
		return;

	// closing infinite cycle 
	EnterCriticalSection(&pool->rw_mutex);
	pool->keep_alive = 0;
	LeaveCriticalSection(&pool->rw_mutex);

	for (int i = 0; i < pool->threads_num; i++)
		SetEvent(pool->threads[i]->event_on_data);

	while (pool->threads_alive != 0) {
		printf("Tried to lie!\n");
		WaitForSingleObject(pool->event_on_state, INFINITE);
	}

	EnterCriticalSection(&pool->rw_mutex);
	printf("Everyone should be dead\n");
	for (int i = 0; i < pool->threads_num; i++)
		thread_destroy(pool->threads[i]);
	LeaveCriticalSection(&pool->rw_mutex);

	DeleteCriticalSection(&pool->rw_mutex);
	
	CloseHandle(pool->event_on_state);
	free(pool);
}


		/*	Task functions	*/

/*
 * Returns:
 * 	Null on error
 */ 

static p_task task_create(void (*on_task)(void *), void *args)
{

	p_task task = malloc(sizeof(t_task));
	if (task == NULL) {
		fprintf(stderr, "task_create: malloc\n");
		return NULL;
	}
	task->on_task = on_task;
	task->args = args;
	return task;
}

static void task_destroy(p_task task)
{
	//free(task);
}

		/* 	Thread functions	*/

static p_thread thread_create(p_pool pool)
{
	p_thread thread = malloc(sizeof(t_thread));
	thread->task_queue = q_create();
	thread->pool = pool;
	thread->event_on_data = CreateEventA(
		NULL, 
		FALSE, // auto reset
		FALSE, // non-signaled
		NULL);


	if (CreateThread(NULL, 0, thread_loop, (void *)thread, 0, NULL) == NULL) {
		fprintf(stderr, "CreateThread: %d\n", GetLastError());
		exit(-1);
	}

	return thread;
}

static void thread_destroy(p_thread thread)
{
	q_destroy(thread->task_queue);
	CloseHandle(thread->event_on_data);
	free(thread);
}


static unsigned long int WINAPI thread_loop(void *t)
{
	p_thread thread_info = (p_thread)t;
	p_pool thread_pool = thread_info->pool;

	// tell everyone you are alive and try grabbing the job right away
	EnterCriticalSection(&thread_pool->rw_mutex);

	thread_pool->threads_alive++;	
	if (thread_pool->threads_alive == thread_pool->threads_num)
		SetEvent(thread_pool->event_on_state);
	thread_pool->threads_working++;
	LeaveCriticalSection(&thread_pool->rw_mutex);

	while (thread_pool->keep_alive) {
		// this queue should be atomic
		p_task task = q_deque(thread_info->task_queue);

		if (task == NULL) {
			
			EnterCriticalSection(&thread_pool->rw_mutex);
			thread_pool->threads_working--;
			// nobody is working, no good
			if (thread_pool->threads_working == 0)
				SetEvent(thread_pool->event_on_state);
			LeaveCriticalSection(&thread_pool->rw_mutex);

			WaitForSingleObject(thread_info->event_on_data, INFINITE);
		}
		else {
			EnterCriticalSection(&thread_pool->rw_mutex);
			//printf("task I got! %d %d\n", task->on_task, task->args);
			thread_pool->threads_working++;
			LeaveCriticalSection(&thread_pool->rw_mutex);

			void (*to_complete)(void *) = task->on_task;
			void *params = task->args;
			to_complete(params);

			task_destroy(task);

		}

	}

	EnterCriticalSection(&thread_pool->rw_mutex);
	thread_pool->threads_working--;
	thread_pool->threads_alive--;

	// last survivor has to tell that all is gone, bye
	if (thread_pool->threads_alive == 0)
		SetEvent(thread_pool->event_on_state);

	printf("Exit.\n");
	fflush(stdout);
	LeaveCriticalSection(&thread_pool->rw_mutex);


	return 0;
}
