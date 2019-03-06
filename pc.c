#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <limits.h>

// DEFINES
#define BUFLEN 5
#define SLEEP_MAX 999999999

// STRUCTS
struct queue{
	int buf[BUFLEN];
	int amount;
	void (*push)(int);
	int (*pop)();
};

// TYPEDEFS
typedef sem_t Semaphore;
typedef pthread_t Thread;
typedef int Consumable;
typedef pthread_mutex_t Mutex;
typedef struct queue Queue;

// GLOBALS
Semaphore emptySlots, fullSlots;
Mutex mutex = PTHREAD_MUTEX_INITIALIZER;
Thread *consumers;
Thread *producers;
int numProducers, numConsumers, sleepyTime;
Queue buffer;
bool running;

// FUNCTION DECLATARIONS
void *producerHandler(void*);
void *consumerHandler(void*);
void placeConsumable(Consumable);
Consumable produce();
Consumable getConsumable();
bool semSetup();
void destroySems();
bool threadSetup(char *argv[]);
void joinThreads();
void cleanup(int);
void queueSetup();
void enqueue(Consumable);
Consumable dequeue();
struct timespec getRandomSleepAmount();

int main(int argc, char *argv[])
{
	//	Command line usage: pc <num producers> <num consumers> <duration>
	if(argc == 4)
	{
		if((sleepyTime = atoi(argv[3])) < 1 || (numProducers = atoi(argv[1])) < 1 || (numConsumers = atoi(argv[2])) < 1)
		{
			fprintf(stderr, "Error: Invalid arguments given to %s\nUsage:\n\t$ %s <# producer threads> <# consumer threads> <total program duration>\n", argv[0], argv[0]);
			exit(1);
		}

		srand(sleepyTime);
		srand(rand());

		if(!semSetup())
		{
			fprintf(stderr, "Error: Unable to properly initialize semaphores. Stopping...\n");
			exit(1);
		}
		if(signal(SIGINT, cleanup) == SIG_ERR)
		{
			fprintf(stderr, "Error: Unable to assign handler to SIGINT. Stopping...\n");
			destroySems();
		}
		running = true;
		queueSetup();
		if(!threadSetup(argv))
		{
			fprintf(stderr, "Error: Unable to properly setup threads. Stopping...\n");
			destroySems();
			exit(1);
		}
		sleep(sleepyTime);
		raise(SIGINT);
	}
	else
	{
		fprintf(stderr, "Error: Invalid arguments given to %s\nUsage:\n\t$ %s <# producer threads> <# consumer threads> <total program duration>\n", argv[0], argv[0]);
		exit(1);
	}
}

// FUNCTION DEFINITIONS

bool semSetup()
{
	if(sem_init(&emptySlots, 0, 5) < 0 || sem_init(&fullSlots, 0, 0))
		return false;
	else
		return true;
}

bool threadSetup(char *argv[])
{
	numProducers = atoi(argv[1]);
	numConsumers = atoi(argv[2]);

	producers = (Thread *)malloc(numProducers * sizeof(Thread));
	consumers = (Thread *)malloc(numConsumers * sizeof(Thread));

	if(producers == NULL || consumers == NULL)
		return false;

	for(int i = 0; i < numProducers; i++)
	{
		Thread current;
		pthread_create(&current, NULL, producerHandler, NULL);
		producers[i] = current;
	}
	for(int i = 0; i < numConsumers; i++)
	{
		Thread current;
		pthread_create(&current, NULL, consumerHandler, NULL);
		consumers[i] = current;
	}
	return true;
}

void queueSetup()
{
	buffer.push = enqueue;
	buffer.pop = dequeue;
	buffer.amount = 0;
}

void cleanup(int signum)
{
	running = false;

	// reset default signal behavior
	signal(SIGINT, SIG_DFL);

	printf("Received SIGINT, performing cleanup...\n");

	// terminate all the running threads
	printf("Terminating threads...\n");
	joinThreads();

	// destroy all of the semaphores
	printf("Destroying semaphores...\n");
	destroySems();

	// terminate
	printf("Done.\n");
	exit(0);
}

void joinThreads()
{
	for(int i = 0; i < numConsumers; i++)
	{
		if(consumers[i] != 0)
			pthread_join(consumers[i], NULL);
	}
	for(int i = 0; i < numProducers; i++)
	{
		if(producers[i] != 0)
			pthread_join(producers[i], NULL);
	}
	free(producers);
	free(consumers);
}

void destroySems()
{
	sem_destroy(&emptySlots);
	sem_destroy(&fullSlots);
}

void *producerHandler(void *args)
{
	while(running)
	{
		Consumable production = produce();
		if(sem_trywait(&emptySlots) != 0)
			continue;
		if(pthread_mutex_trylock(&mutex) != 0)
			continue;
		printf("Produced %d.\n", production);
		buffer.push(production);
		pthread_mutex_unlock(&mutex);
		sem_post(&fullSlots);

		struct timespec waitTime = getRandomSleepAmount();
		nanosleep(&waitTime, NULL);
	}

	printf("Producer terminating...\n");

	return NULL;
}

void *consumerHandler(void *args)
{
	while(running)
	{
		if(sem_trywait(&fullSlots) != 0)
			continue;
		if(pthread_mutex_trylock(&mutex) != 0)
			continue;
		Consumable c = buffer.pop();
		printf("Consumed %d.\n", c);
		pthread_mutex_unlock(&mutex);
		sem_post(&emptySlots);

		struct timespec waitTime = getRandomSleepAmount();
		nanosleep(&waitTime, NULL);
	}

	printf("Consumer terminating...\n");
	return NULL;
}

Consumable produce()
{
	return (Consumable)rand();
}

void enqueue(Consumable c)
{
	buffer.buf[buffer.amount] = c;
	buffer.amount++;
}

Consumable dequeue()
{
	if(buffer.amount > 0)
	{
		buffer.amount--;
		Consumable val = buffer.buf[0];
		for(int i = 0; i < BUFLEN-1; i++)
		{
			buffer.buf[i] = buffer.buf[i+1];
		}
		return val;
	}
	else
	{
		// This should never happen!!
		return -1;
	}
}

struct timespec getRandomSleepAmount()
{
	struct timespec waitTime = {0, rand() % SLEEP_MAX};
	return waitTime;
}