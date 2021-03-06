/*
 * File: bucketsort.c
 * Author: Katie Levy
 * Date: 12/4/16
 * Description: A parallel distributed systems program using MPI to 
 * sort an array of random integers efficiently using bucket sort 
 * amoungst various processes.
 */

#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<time.h>
#include<sys/time.h>
#include<math.h>
#include<mpi.h>
#include<unistd.h>

 /* Function declarations */
int serialsort(int size, int unsorted[], int tempA[]);
int mergeSort(int start, int stop, int unsorted[], int tempA[]);
int merge(int start, int middle, int stop, int unsorted[], int tempA[]);
int validateSerial();
int validateParallel();
void printArray(int arr[], int size);
int createPivots();
int divideIntoBuckets();
int sendBuckets();
int kWayMerge(int k, int unsorted[], int tempA[]);



/* Global variables */
int comm_sz;
long n; //array size
int *vecSerial;
int *vecParallel;
int *temp;
int *pivots;
int *local_vecParallel;
int local_n;
int my_id, root_process, ierr;
int *valsInBuckets;
int *myArrToSort;
int *bucketStop;
int numToSort;
int *recvBucketStop;

struct node {
    int value;
    struct node *next;
};
typedef struct node node;

struct bucket {
    int size;
    struct node *linkedList;
};
typedef struct bucket bucket;

/*--------------------------------------------------------------------*/
int main(int argc, char* argv[]){
    MPI_Status status;
    ierr = MPI_Init(&argc, &argv);
    ierr = MPI_Comm_rank(MPI_COMM_WORLD, &my_id);
    ierr = MPI_Comm_size(MPI_COMM_WORLD, &comm_sz);
    pivots = (int *) malloc(sizeof(int) * comm_sz-1);

    // Process 0
    if( my_id == 0 ) {
        // Get n from standard input
        printf("Enter the size of the array:\n");
        scanf("%ld", &n);
        while(n % comm_sz != 0){
            printf("Please enter an array size divisable by the number of processes:\n");
            scanf("%ld", &n);
        }
        
        // For timing
        struct timeval  tv1, tv2;

        // Allocate memory for global arrays
        vecSerial = (int *) malloc(sizeof(int) * n);
        vecParallel = (int *) malloc(sizeof(int) * n);
        temp = (int *) malloc(sizeof(int) * n); 
        int i; 

        // Fill the arrays with the same random numbers
        srand(time(NULL));
        for(i = 0; i < n; i++){
            int random = rand() % 100;
            vecSerial[i] = random;
        }

        // Copy first array to second array
        memcpy(vecParallel, vecSerial, sizeof(int)*n);
        memcpy(temp, vecSerial, sizeof(int)*n);

        // Perform the serial mergesort
        gettimeofday(&tv1, NULL); // start timing
        serialsort(n, vecSerial, temp);
        gettimeofday(&tv2, NULL); // stop timing
        double serialTime = (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec);
        // Print results.
        validateSerial();
        //printArray(vecSerial, n);
        
        // Perform the parallel bucketsort
        gettimeofday(&tv1, NULL); // start timing
        
        // Calculate the pivots
        createPivots();
        
        // Broadcast n and pivots to other procs
        MPI_Bcast(&n, 1, MPI_LONG, 0, MPI_COMM_WORLD);
        MPI_Bcast(pivots, comm_sz - 1, MPI_INT, 0, MPI_COMM_WORLD);
        
        // Distribute vecParallel to different processes with block distribution
        // local_n is number of elems per proc
        local_n = n / comm_sz;
        local_vecParallel = (int *)malloc(sizeof(int) * local_n);
        MPI_Scatter(vecParallel, local_n, MPI_INT, local_vecParallel, local_n,
            MPI_INT, 0, MPI_COMM_WORLD);

// BODY OF ALG:

        divideIntoBuckets();
        sendBuckets();
        int *tempMyToSort = (int *)malloc(sizeof(int)*numToSort);

        kWayMerge(comm_sz, myArrToSort, tempMyToSort);
        //serialsort(numToSort, myArrToSort, tempMyToSort);
        free(tempMyToSort);
        // Copy my elements to the large array
        memcpy(&vecParallel[0], &myArrToSort[0], sizeof(int)*numToSort);
        int index = numToSort;
        MPI_Status status;
        // Receive all the pieces from the procs
        for(i = 1; i < comm_sz; i++){
            MPI_Recv(&vecParallel[index], n, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
            MPI_Get_count(&status, MPI_INT, &numToSort);
            index += numToSort;
        }
        //printf("Final Arrray:\n");
        //printArray(vecParallel, n);

        gettimeofday(&tv2, NULL); // stop timing
        double parallelTime = (double) (tv2.tv_usec - tv1.tv_usec) / 1000000 +
            (double) (tv2.tv_sec - tv1.tv_sec);
        //printArray(vecParallel, n);
        // Validate parallel array is correct
        validateParallel();

        // Print results
        double speedup = serialTime / parallelTime;
        double efficiency = speedup / comm_sz;
        printf("Number of processes: %d\n", comm_sz);
        printf("Array Size: %ld\n", n);
        printf("Time to sort with serial merge sort: %e\n", serialTime);
        printf("Time to sort with parallel bucket sort: %e\n", parallelTime);
        printf("Speedup: %e\n", speedup);
        printf("Efficincy: %e\n", efficiency);

        free(vecSerial);
        free(vecParallel);
        free(temp);
        free(myArrToSort);
    } else {
// Other processes        
        // Broadcast to recieve n
        MPI_Bcast(&n, 1, MPI_LONG, 0, MPI_COMM_WORLD);
        MPI_Bcast(pivots, comm_sz - 1, MPI_INT, 0, MPI_COMM_WORLD);

        // Distribute vecParallel to different processes with block distribution
        local_n = n / comm_sz;  // local_n is number of elems per proc
        local_vecParallel = (int *)malloc(sizeof(int) * local_n);
        MPI_Scatter(vecParallel, local_n, MPI_INT, local_vecParallel, local_n,
            MPI_INT, 0, MPI_COMM_WORLD);
        
// BODY OF ALG:
        divideIntoBuckets();
        sendBuckets();
        int *tempMyToSort = (int *)malloc(sizeof(int)*numToSort);
        kWayMerge(comm_sz, myArrToSort, tempMyToSort);
        // Send sorted array to Process 0
        MPI_Send(myArrToSort, numToSort, MPI_INT, 0, 0, MPI_COMM_WORLD);

        free(myArrToSort);
        free(tempMyToSort);
    }
    free(bucketStop);
    free(local_vecParallel);
    ierr = MPI_Finalize();
    return 0;
}

// Returns 0 on success and 1 on failure
int serialsort(int size, int unsorted[], int tempA[]){
    if(!(mergeSort(0, size -1, unsorted,  tempA))){
        return 0;
    }else{
        return 1;
    }
}

// Serial mergesort
int mergeSort(int start, int stop, int unsorted[], int tempA[]){
    if(start >= stop){
        return 0;
    }
    int middle = ((stop + start) / 2);
    mergeSort(start, middle, unsorted,  tempA);
    mergeSort(middle+1, stop, unsorted,  tempA);
    merge(start, middle, stop, unsorted,  tempA);
    return 0;
}

// Merge portion of mergesort
int merge(int start, int middle, int stop, int unsorted[], int tempA[]){
    int first = start;
    int second = middle+1;
    int tempIndex = start;
    while(first <= middle && second <= stop){
        if(unsorted[first] < unsorted[second]){
            tempA[tempIndex] = unsorted[first];
            first++;
            tempIndex++;
        } else {
            tempA[tempIndex] = unsorted[second];
            second++;
            tempIndex++;
        }
    }
    while(first <= middle){
        tempA[tempIndex] = unsorted[first];
            first++;
            tempIndex++;
    }
    while(second <= stop){
        tempA[tempIndex] = unsorted[second];
            second++;
            tempIndex++;
    }
    int i;
    for(i = start; i <= stop; i++){
        unsorted[i] = tempA[i];
    }
    return 0;
}

// Verify that the serial mergesort is correct
int validateSerial(){
    int i;
    for(i = 0; i < n-1; i++){
        if(vecSerial[i] > vecSerial[i+1]){
            printf("Serial sort unsuccesful.\n");
            return 1;
        }
    }
    return 0;
}

// Verify the parallel bucketsort is correct by comparing the arrays
int validateParallel(){
    int i;
    for(i = 0; i < n-1; i++){
        if(vecSerial[i] != vecParallel[i]){
            printf("Parallel sort unsuccesful.\n");
            return 1;
        }
    }
    return 0;
}

// Print array passed in as argument
void printArray(int arr[], int size){  
    int i;
    for(i = 0; i < size; i++){
        printf("%d\t", arr[i]);
    }
    printf("\n");
    return;
}

// Creates the pivots for procs to know to which 
// buckets to put elems into
int createPivots(){
    // Process 0 computes pivots
    int s = (int) 10 * comm_sz * log2(n);
    int *samples;
    int *samples_temp;
    int i, random, index;
    int *samplesIndexSet;
    // Check if sample size is larger than array size
    // Then all values in array are samples
    if(s > n){
        s = n;
        samples = (int *) malloc(sizeof(int) * s);
        samples_temp = (int *) malloc(sizeof(int) * s);
        memcpy(samples, vecParallel, s*sizeof(int));
    } else {
        samples = (int *) malloc(sizeof(int) * s);
        samples_temp = (int *) malloc(sizeof(int) * s);
        samplesIndexSet = (int *)malloc(sizeof(int)*s);
        // Floyd sampling without replacement
        index = 0;
        for(i = n - s; i < n; i++){
            random = rand() % i;
            if(samplesIndexSet[random] == 0){
                samples[index] = vecParallel[random];
                samplesIndexSet[random] = 1;
            } else {
                samples[index] = vecParallel[i];
                samplesIndexSet[i] = 1;
            }
            index++;
        }
        free(samplesIndexSet);
    }
    serialsort(s, samples, samples_temp);
    for(i = 0; i < comm_sz - 1; i++){
        pivots[i] = samples[((i+1) * s) / comm_sz];
    }
    free(samples);
    free(samples_temp);
    return 0;
}

// Divide the values received from Process 0 into buckets to send 
// to other processes
int divideIntoBuckets(){
    int i;
    int *tempbucket = (int *) malloc(sizeof(int) * local_n);
    serialsort(local_n, local_vecParallel, tempbucket);
    free(tempbucket);
    bucketStop = (int *) malloc(sizeof(int) * comm_sz);
    int bucketNum = 0;
    
    for(i = 0; i < local_n; i++){
        // Determine bucket stop
        if(local_vecParallel[i] >= pivots[bucketNum]){
            while(local_vecParallel[i] >= pivots[bucketNum]){
                bucketStop[bucketNum] = i;
                bucketNum++;
                if(bucketNum == comm_sz - 1){
                    break;
                }
            }
        }
        if(bucketNum == comm_sz - 1){
            break;
        }
    }
    while(bucketNum < comm_sz){
        bucketStop[bucketNum] = local_n;
        bucketNum++;
    }
    //printf("Proc %d bucket indices:\t", my_id);
    //printArray(bucketStop, comm_sz);
    //printArray(local_vecParallel, local_n);
    free(pivots);
    return 0;
}

// Send the buckets to other processes for them to sort later
int sendBuckets(){
    // allocate memory for an array of vals to sort
    myArrToSort = (int *) malloc(sizeof(int)*local_n * 2);
    recvBucketStop = (int *) malloc(sizeof(int)*comm_sz);
    int myArrSize = local_n * 2;
    //  check if index greater than size
    MPI_Status status;
    int i = 0;
    int index = 0;
    for(i = 0; i < comm_sz; i++){
        int sendcount, numElems, start;
        // Determine number of elems to send and where they start
        if(i == 0){
            sendcount = bucketStop[0] - 0;
            start = 0;
        } else {
            sendcount = bucketStop[i] - bucketStop[i-1];
            start = bucketStop[i-1];
        }
        
        if(i == my_id){
            // If looking at own bucket, add values to myArrToSort
            memcpy(&myArrToSort[index], &local_vecParallel[start], sizeof(int)*sendcount);
            index += sendcount;
            
        } else{
            // Send values in bucket i to process i and receive values from process i
            MPI_Sendrecv(&local_vecParallel[start], sendcount, MPI_INT, i, 123, 
                &myArrToSort[index], local_n, MPI_INT, i, 123, MPI_COMM_WORLD, &status);
            MPI_Get_count(&status, MPI_INT, &numElems);
            index += numElems;
            // Reallocate memory if myArrToSort is full
            if(index > myArrSize){
                printf("Reallocating memory\n");
                myArrToSort = (int *) realloc(myArrToSort, sizeof(int)*local_n);
            }
        }
        recvBucketStop[i] = index;
    }
    //printf("Proc %d i is %i myArrToSort:\n", my_id, i);
    //printArray(myArrToSort, index);
    numToSort = index;
    return 0;
}

// Merge k different sorted arrays into one
int kWayMerge(int k, int unsorted[], int tempA[]){
    int *start = (int *) malloc(sizeof(int) * k);
    int i;
    for(i = 0; i < k; i++){
        if(i == 0){
            start[0] = 0;
        } else {
            start[i] = recvBucketStop[i -1];
        }
    }
    int tempIndex = 0;
    int min, minProc;
    int valueLeft = 0; //true

    while(valueLeft == 0){
        min = 10000;
        minProc = -1;
        // Find the minimum value from all k arrays' start
        for(i = 0; i < k; i++){
            if(start[i] < recvBucketStop[i]){
                if(unsorted[start[i]] < min){
                    min = unsorted[start[i]];
                    minProc = i;
                }
            }
        }
        // Check if no more elements
        if(minProc == -1){
            valueLeft = -1;
        } else {
            // Add to temp array with the value found
            tempA[tempIndex] = unsorted[start[minProc]];
            tempIndex++;
            start[minProc]++;
        }
    }
    for(i = 0; i <= recvBucketStop[k-1]; i++){
        unsorted[i] = tempA[i];
    }
    free(start);
    return 0;
}



