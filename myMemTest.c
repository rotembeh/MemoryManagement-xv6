
#include "types.h"
#include "stat.h"
#include "user.h"
#include "param.h"



#define pagesAmount 17


//TEST 1 and 2 : framework: fork, allocuvm, swapin, swapout, deallocuvm and all the framework.
//TEST 3 and 4 : checking the 4 SELECTION types.
int
main(int argc, char *argv[]){
	int flag=1;
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

    printf(2,"----------------------------------------\n\n");
    printf(2, "TEST 2:\n"); 
    int* arr2[pagesAmount];
    void* arr3[pagesAmount];
    //father making his mem big, with pages on disk
    int test2pid = fork();
    if (test2pid == 0){ //child
	    printf(2, "Father (pid: %d) making pages on disk.\n", getpid());
	    for (int k=0; k<pagesAmount; k++){ //time spending 
	    	printf(2,"MALLOC ITERATION\n"); 
	   		arr2[k] = (int*)malloc(4096);
	    }
	    printf(2, "Father (pid: %d) Forking.\n", getpid());
	    int x = fork();
	    if (x == 0){ //child
	    	printf(2, "child (pid: %d): going to malloc with %d*%d nbytes.\n",getpid(), 3, 4096);
	    	for (int k=0; k<3; k++){
	    		printf(2,"MALLOC ITERATION (%d left)\n", 3-k);
	   			arr3[k] = (void*)malloc(4096);
	    	}
	    	printf(2, "child: going to free.\n");
	        for (int k=0; k<3; k++)
	   			free(arr3[k]);		
	   		exit();
	    }
	    else{
	    //	sleep(200);
	    	wait();
	    } //TEST 2.5:::
	   	printf(2, "Father (pid: %d) Going to use some addresses.\n", getpid());
	   	*arr2[1] = 0;
	   	*arr2[2] = 9;
	    *arr2[16] = 100;
	   	*arr2[8] = 7;
	   	*arr2[11] = 7;
		*arr2[5] = 7;
		*arr2[6] = 2;
	    *arr2[13] = 7;
	   	*arr2[12] = 7;
	   	*arr2[9] = 7;
		*arr2[10] = 70;
	   	*arr2[14] = 1;
		*arr2[15] = 1;
		*arr2[3] = 0;
		*arr2[4] = 0;
		*arr2[7] = 0;
		*arr2[0] = 0;

	//    *arr2[13] = 7;

	//    printf(2, "arr2[13] = %d, address= %d\n", *arr2[13], arr2[13]);
		printf(2,"check sum\n");
	//			printf(2,"%d + %d - %d\n", *arr2[2],*arr2[6],*arr2[17]);
		for (int j = 0 ; j < pagesAmount; j++)
			printf(2, "arr2[%d] = %d = %d\n", j, arr2[j], *arr2[j]);

		*arr2[1] = (int)*arr2[2] + (int)*arr2[6] - (int)*arr2[14]; //9 + 2 - 1 == 10
		if (*arr2[1] == 10)
			printf(2, "Test2 check sum OK\n");
		else{
			printf(2, "Test2 check sum FAULT (%d)\n", (int)*arr2[1]);
			flag=-1;
		}
		printf(2, "father: going to check inherited pages. foriking...\n");
	    int child2 = fork();
	    if (child2==0)//child checks that the pages inherited well
	    {
    	int sum = 0;
	    	for (int j = 0; j<pagesAmount; j++)
	    		sum += *arr2[j];
	    		    	if (sum == 235) //225 + 10 because the last test
	    		printf(2, "Test2 check sum 2 OK\n");
			else{
				printf(2, "Test2 check sum 2 FAULT (%d)\n", sum);
				flag=-1;
			}
			
//			int sum = (int)*arr2[19] + (int)*arr2[10] - (int)*arr2[18];
//	    	if (sum == 169)
//	    		printf(2, "Test2 check sum 2 OK\n");
//			else
//				printf(2, "Test2 check sum 2 FAULT (%d)\n", sum);
			exit();
	    }
	    else
	    	wait();
	    printf(2, "Father (pid: %d) going to free.\n", getpid());
        for (int k=0; k<pagesAmount; k++){ 
			free(arr2[k]);		
	    }
	    exit();
	}
	else
		wait();

    printf(2, "FINISH TEST 2.\n", getpid());

    #if defined LAPA | defined NFUA

    printf(2, "----------------------------------\n\nTEST 3:\n");
    //expected 3 faults for lapa and 0 faults for nfua.
    int test3pid=fork();
    if (test3pid == 0){
    char *arr5[16];
    //for NFUA and LAPA. trying to choose page to swapOut before 32 ticks threw after accessed (thats why not
    //many prints).

    sleep(40); //ticks to make counters 0 bits (init in lapa is FFFFFFFF)
    printf(2, "DEFINING X PAGE...\n");
    char *x = sbrk(4096);
  //  printf(2, "ACCESSING X PAGE ALOT OF TIMES...\n");

    //now X page is with many 1 bits on the aging array.
    for (int i = 0; i<32; i++){
    	*x = i;
    }
//    printf(2, "DEFINING Y PAGE...\n");
 //   printf(2, "DEFINING Z PAGE...\n");
    char *y = sbrk(4096);
    char *z = sbrk(4096);
    *x = 2; *y = 3;
    *y = 2;	*z = 3;
    *z = 2; *x = 3;
    sleep(50); //turning off all the bits in x y z counters.
    //making 9 pages to fill the ram to maximum capacity
    for (int i = 0; i<9; i++){
    	arr5[i] = sbrk(4096);
    }
    *arr5[7]=1;
    //improving x y and z
    sleep(1);
    
    *x = 7;
    *y = 10;
	*z = 8;

	sleep(1); //making sure we have a tick to update the aging counter before swaping out pages.

	//Here the program needs to choose which pages to swap out (4 of them).
    //LAPA SWAPOUT X Y AND Z. NFUA NOT SWAPING OUT EACH OF THEM -> 0 PageFaults.
    //X Y and Z are the last pages we chose. so the counter is bigger -> nfua is not choosing them.
    //X Y and Z has small amount of 1 bits because many ticks threw. the others has init after and they have many
    //1bits-> LAPA won't choosing the other and DO choose X Y and Z.

    for (int i = 0; i<3; i++){
    	arr5[i] = sbrk(4096);
    }
    //lapa needs to swapin y and z. nfua needs to swapin only x.
    printf(2,"NEEDS TO LOAD X PAGE\n");
    *x = 11;
    printf(2,"NEEDS TO LOAD Y PAGE\n");
    *y = 12;
    printf(2,"NEEDS TO LOAD Z PAGE\n");
    *z = 5;
  /*  if (*x+*y != 17){
    	printf(2,"test3 fault\n"); 
    	flag=-1;
    }
    else
    	printf(2,"test3 OK\n");*/
    printf(2, "FINISH TEST 3\n");
	exit();
	}
	else
		wait();
	#endif
  
    #ifdef AQ

	printf(2, "----------------------------------\n\nTEST 4 (similar to test 3, see AQ other results):\n");
    //expecting 1 page fault (on z)
    int test4pid=fork();
    if (test4pid == 0){
    char *arr5[16];
    //LIKE Test 3 With Change with AQ results.

    sleep(40); //ticks to make counters 0 bits (init in lapa is FFFFFFFF)
    printf(2, "DEFINING X PAGE...\n");
    char *x = sbrk(4096);
  //  printf(2, "ACCESSING X PAGE ALOT OF TIMES...\n");

    //now X page is with many 1 bits on the aging array.
    //page y with only 1 bit, but the counter is bigger than x.
    for (int i = 0; i<32; i++){
    	*x = i;
    }
//    printf(2, "DEFINING Y PAGE...\n");
 //   printf(2, "DEFINING Z PAGE...\n");
    char *y = sbrk(4096);
    char *z = sbrk(4096);
    *x = 2; *y = 3;
    *y = 2;	*z = 3;
    *z = 2; *x = 3;
    sleep(1);
    //making 9 pages to fill the ram to maximum capacity
    for (int i = 0; i<9; i++){
    	arr5[i] = sbrk(4096);
    }
    *arr5[7]=1;
    //improving x and y 
        sleep(1);
    printf(2,"Improving x and y in queue (AQ)\n");
    for(int i=0;i<100;i++){
    	*x = i; sleep(1);
    }
    for(int i=0;i<100;i++){
    	*y = i; sleep(1);
    }
	//Here the program needs to choose which pages to swap out (4 of them).
    //LIKE TEST 3, but here x and y should be better on the AQ because they get accessed 100 times.

    for (int i = 0; i<4; i++){
    	arr5[i] = sbrk(4096);
    }
    printf(2,"Expecting that only Z is going to SwapIn (on AQ)\n");
    printf(2,"NEEDS TO LOAD X PAGE\n");
    *x = 11;
    printf(2,"NEEDS TO LOAD Y PAGE\n");
    *y = 12;
    printf(2,"NEEDS TO LOAD Z PAGE\n");
    *z = 5;

    printf(2, "FINISH TEST 4\n");
	exit();
	}
	else
		wait();

	#endif

	#ifdef SCFIFO
	printf(2, "----------------------------------\n\nTEST 5 (for SCFIFO):\n");
	//expected 1 fault on y.
    int test5pid=fork();
    if (test5pid == 0){
    char *arr5[12];

    char *x=0; char *y=0;
    //LIKE Test 3 With Change with SCFIFO results.

    sleep(40); //ticks to make counters 0 bits (init in lapa is FFFFFFFF)
    //making 9 pages to fill the ram to maximum capacity
    printf(2,"Making Pages\n");
    for (int i = 0; i<9; i++){
    	if (i == 3)
    	{
    		printf(2, "DEFINING X PAGE...\n");
    		x = sbrk(4096); //10 in ram array
    	}
    	if (i == 5){
    		printf(2, "DEFINING Y PAGE...\n");
    		y = sbrk(4096); //13 in ram array
    	}
    	arr5[i] = sbrk(4096);
    }
    *arr5[0]=1;

    printf(2, "DEFINING Z PAGE...\n");
    char *z = sbrk(4096);  

    sleep(10);

    printf(2, "Accessing z\n");
    *z = 9;
  //  printf(2, "Accessing y\n");
  //  *y = 50;
    printf(2, "Accessing x\n");
    *x = 16;
    printf(2, "Done Accessing x, z\n");

    printf(2, "Choosing 8 pages to out. it won't be x or z. y should be out because y's PTE_A if off\n");
    for (int i = 0; i<8; i++){
    	arr5[i] = sbrk(4096);
    }
    printf(2,"Expecting that only y is going to SwapIn(on SCFIFO)\n");
    printf(2,"NEEDS TO LOAD Z PAGE\n");
    *z = 11;
    sleep(1);
    printf(2,"NEEDS TO LOAD Y PAGE\n");
    *y = 12;
    sleep(1);
    printf(2,"NEEDS TO LOAD X PAGE\n");
    *x = 5;
    printf(2,"Done\n");

    sleep(1);
    printf(2, "FINISH TEST 5\n");
	exit();
	}
	else
		wait();

	#endif

    if (flag==1)
    	printf(2,"tests passed (1 and 2)!\n");

    printf(2, "Father (pid: %d) exit.\n", getpid());
    exit();
}
        //to check: why if i change in copyuvm the && to & we dont get error. (it should try to copy
        //from empty page in the ram but its not (in the else case for disk pages in the father))
