#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  //Header file for sleep(). man 3 sleep for details. 
#include <pthread.h>

// A normal C function that is executed as a thread  
// when its name is specified in pthread_create() 
int myThreadFun(void *vargp)
{
    sleep(1);
    printf("Printing GeeksQuiz from Thread \n");
    return 57;
}

int main() {
    pthread_t thread_id;
    void *returnValue;
    printf("Before Thread\n");
    pthread_create(&thread_id, NULL, (void * (*)(void*)) myThreadFun, NULL);
    pthread_join(thread_id, &returnValue);
    printf("After Thread\n");
    printf("return value: %d\n",  (int)returnValue);
    exit(0);
}