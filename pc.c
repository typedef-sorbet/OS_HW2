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
#define BUFLEN 5								// How much data the buffer in the queue should hold
#define SLEEP_MAX 999999999						// Max amount of nanoseconds any thread will sleep for

// STRUCTS
struct queue{									// Queue to hold the consumable data that producers produce, and consumers consume.
	int buf[BUFLEN];								// Actual buffer to hold the data.
	int amount;										// Amount of valid data in the buffer (set by push and pop).
	void (*push)(int);								// Adds a new value to the buffer.
	int (*pop)();									// Removes and returns a value from the buffer.
};

// TYPEDEFS
typedef sem_t Semaphore;
typedef pthread_t Thread;
typedef int Consumable;
typedef pthread_mutex_t Mutex;
typedef struct queue Queue;

// GLOBALS
Semaphore emptySlots, fullSlots;				// Semaphores for detecting empty and full slots in the global queue respectively.
Mutex mutex = PTHREAD_MUTEX_INITIALIZER;		// Mutex for accessing the queue.
Thread *consumers;								// List of consumer threads.
Thread *producers;								// List of producer threads.
int numProducers, numConsumers, sleepyTime;		// Number of producer and consumer threads; amount of time that each thread will run (approx.).
Queue buffer;									// Global queue of integers that producers write to, and consumers read from.
bool running;									// Global flag that producer and consumer threads check to see if they should continue trying to produce or consume.

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
	//	Expected argc: 4

	if(argc == 4)
	{
		//	If any of the arguments are out of range, throw an error and exit
		if((sleepyTime = atoi(argv[3])) < 1 || (numProducers = atoi(argv[1])) < 1 || (numConsumers = atoi(argv[2])) < 1)
		{
			fprintf(stderr, "Error: Invalid arguments given to %s\nUsage:\n\t$ %s <# producer threads> <# consumer threads> <total program duration>\n", argv[0], argv[0]);
			exit(1);
		}

		// seed the RNG
		srand(sleepyTime);
		srand(rand());

		// if the semaphore setup fails, exit
		if(!semSetup())
		{
			fprintf(stderr, "Error: Unable to properly initialize semaphores. Stopping...\n");
			exit(1);
		}
		// if we can't reset the signal handler, exit
		if(signal(SIGINT, cleanup) == SIG_ERR)
		{
			fprintf(stderr, "Error: Unable to assign handler to SIGINT. Stopping...\n");
			destroySems();
			exit(1);
		}

		// we're ready to start running
		running = true;
		queueSetup();

		// if thread setup fails, exit
		if(!threadSetup(argv))
		{
			fprintf(stderr, "Error: Unable to properly setup threads. Stopping...\n");
			destroySems();
			exit(1);
		}

		// wait for the specified amount of time
		sleep(sleepyTime);
		// raise SIGINT to stop all threads and execute cleanup
		raise(SIGINT);
	}
	else
	{
		fprintf(stderr, "Error: Invalid arguments given to %s\nUsage:\n\t$ %s <# producer threads> <# consumer threads> <total program duration (sec)>\n", argv[0], argv[0]);
		exit(1);
	}
}

// FUNCTION DEFINITIONS

//	Initializes the semaphores used for tracking the number of full and empty slots in the queue.
//	Returns a truth value corresponding to whether or not the function was successful
//	in initializing the semaphores.
bool semSetup()
{
	// if either init call returns an error, return a failure
	if(sem_init(&emptySlots, 0, BUFLEN) < 0 || sem_init(&fullSlots, 0, 0) < 0)
		return false;
	else
		return true;
}

//	Creates and starts the specified number of producer and consumer threads.
//	Returns a bool corresponding to the success of creating and starting said threads.
bool threadSetup(char *argv[])
{
	// allocate space for the threads
	producers = (Thread *)malloc(numProducers * sizeof(Thread));
	consumers = (Thread *)malloc(numConsumers * sizeof(Thread));

	// check if the mallocs failed; if they did, return failure
	if(producers == NULL || consumers == NULL)
		return false;

	// create the threads and store them in the appropriate array
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

	// return success
	return true;
}

//	Sets up the Queue structure that holds Consumables.
void queueSetup()
{
	buffer.push = enqueue;
	buffer.pop = dequeue;
	buffer.amount = 0;
}

//	SIGINT handler that waits until all spawned threads terminate, and then destroys the global semaphores.
//	This function does not return.
void cleanup(int signum)
{
	// set running flag to false so that threads know to terminate
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

//	Waits until each thread in the thread arrays has terminated, and then frees their respective memory.
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

//	Destroys the global semaphores.
void destroySems()
{
	sem_destroy(&emptySlots);
	sem_destroy(&fullSlots);
}

//	Producer thread function.
//	"Produces" a random number to insert into the queue, and then waits for both an empty slot and the queue mutex
//	before it inserts that number into the queue to later be "consumed". The thread then releases the queue mutex
// 	and posts to the fullSlots semaphore so that any waiting consumer threads can consume valid data in the queue.
//	The producer then waits for some random amount of time in the range of [0, SLEEP_MAX) nanoseconds.
void *producerHandler(void *args)
{
	// while we're still within the desired duration...
	while(running)
	{
		// produce some data
		Consumable production = produce();

		// try to get a hold on an empty slot; if we can't, loop back up.
		if(sem_trywait(&emptySlots) != 0)
			continue;

		// try to get a lock on the queue mutex; if we can't, post back the empty slot, loop back up.
		if(pthread_mutex_trylock(&mutex) != 0)
		{
			sem_post(&emptySlots);
			continue;
		}

		printf("Produced %d.\n", production);

		// push the data onto the queue
		buffer.push(production);

		// give back queue mutex
		pthread_mutex_unlock(&mutex);

		// post a full slot for consumers to detect
		sem_post(&fullSlots);

		// sleep for a little bit
		struct timespec waitTime = getRandomSleepAmount();
		nanosleep(&waitTime, NULL);
	}

	return NULL;
}

//	Consumer thread function.
//	Waits for a full slot in the buffer, then attempts to get the queue mutex. If it is successful,
//	the consumer will pop the next value off of the queue, print it out, and then release the queue lock
//	and post to the emptySlots semaphore so that any waiting producer threads can place new data in the queue.
//	The consumer then waits for some random amount of time in the range of [0, SLEEP_MAX) nanoseconds.
void *consumerHandler(void *args)
{
	// while we're still within the desired duration...
	while(running)
	{
		// try to get a hold on a full slot; if we can't, loop back up.
		if(sem_trywait(&fullSlots) != 0)
			continue;
		// try to get a lock on the queue mutex; if we can't, post back the full slot, loop back up.
		if(pthread_mutex_trylock(&mutex) != 0)
		{
			sem_post(&fullSlots);
			continue;
		}

		// grab some data
		Consumable c = buffer.pop();
		printf("Consumed %d.\n", c);

		// give back queue mutex
		pthread_mutex_unlock(&mutex);

		// post empty slot for producers to detect
		sem_post(&emptySlots);

		// sleep for a little bit
		struct timespec waitTime = getRandomSleepAmount();
		nanosleep(&waitTime, NULL);
	}

	return NULL;
}

//	Wrapper function for rand().
Consumable produce()
{
	return (Consumable)rand();
}

//	Inserts c into the next available slot in buffer.buf.
//	Doesn't need to do range checks, since this code will only run if there is a guaranteed
//	empty slot in buffer.buf.
void enqueue(Consumable c)
{
	buffer.buf[buffer.amount] = c;
	buffer.amount++;
}

//	Returns the value at the front of buffer.buf and moves everything in the list up.
// 	Doesn't *technically* need to do range checks (for the reasons stated above),
//	but I do so anyway just in case. Better to have an invalid pop than a segfault, IMO.
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

//	Returns a struct timespec with a random amount of milliseconds between [0, SLEEP_MAX).
//	This value gets used for the producer/consumer threads' calls to nanosleep.
struct timespec getRandomSleepAmount()
{
	struct timespec waitTime = {0, rand() % SLEEP_MAX};
	return waitTime;
}