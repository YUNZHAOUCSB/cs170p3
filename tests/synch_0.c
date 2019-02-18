#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  //Header file for sleep(). man 3 sleep for details. 
#include <pthread.h>

// A normal C function that is executed as a thread  
// when its name is specified in pthread_create() 
int *myThreadFun(void *vargp)
{
    sleep(1);
    printf("Printing GeeksQuiz from Thread \n");
    return 23;
}

int main() {
    pthread_t thread_id;
    printf("Before Thread\n");
    pthread_create(&thread_id, NULL, myThreadFun, NULL);
    pthread_join(thread_id);
    printf("After Thread\n");
    exit(0);
}