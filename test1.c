 

#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"



#define pagesAmount 20

//actually malloc first allocates some pages (i saw 3) and then use it.
int
main(int argc, char *argv[]){
    printf(2, "myMem Tests:\n");
    void* arr[pagesAmount];
    int test1pid = fork();
    if (test1pid == 0){ //child
	    printf(2, "TEST 1:\n");
	    printf(2, "test1: going to malloc with %d*%d nbytes.\n",pagesAmount, 4096);
	    for (int k=0; k<pagesAmount; k++){
	    	printf(2,"MALLOC ITERATION\n");
	   		arr[k] = (void*)malloc(4096);
	    }
	    printf(2, "going to free.\n");
	    for (int k=0; k<pagesAmount; k++){ 
	   		free(arr[k]);		
	    }
	    exit();
	}
	else 
		wait(); //clean all the pages of test1pid
    printf(2, "FINISH TEST 1\n");    
    printf(2, "Father (pid: %d) exit.\n", getpid());
    exit();
}
