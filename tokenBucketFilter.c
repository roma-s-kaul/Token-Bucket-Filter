
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>

#include "template.h"
#include "list.h"

typedef struct tagMy402Packet{
    int id;
    int tokensNeeded;
    double interArrivalTime;
    /*post mutex in q1*/
    double arrivalTime;
    double q1EnterTime;
    double q1ExitTime;
    double q2EnterTime;
    double q2ExitTime;
    int serviceTime;
    double serviceStartTime;
    double serviceEndTime;
}My402Packet;

#define MAX_BPN 2147483647
#define MAX_TIME 10000

/*Token bucket parameters*/
double tokenRate;
int bucketSize;
int availableTokens;
bool tokenThreadRunning;
int droppedTokens;
int tokenCounter;

/*Packet parameters*/
double packetRate;
int tokensPerPacket;
int numPackets;
bool packetThreadRunning;
int droppedPackets;
int totalPackets;
int processedPackets;

/*Server parameter*/
double serverRate;

/*Mode of emulation*/
bool isDeterministic;
char* tsFile;

struct timeval emulationStartTime;
struct timeval emulationEndTime;

/*Stats Variables*/
double totalIntervalTime;
double totalServiceTime;
double totalTimeInSystem; 
double totalTimeInSystemSq;
double emulationTime;
double totalTimeInQ1;
double totalTimeInQ2;
double totalTimeInS1;
double totalTimeInS2;

/*Synchronization variables*/
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

/*Thread variables*/
pthread_t cntrlCThread, packetThread, tokenThread, serverOneThread, serverTwoThread;

/*Queue variables*/
My402List q1,q2;

bool isShutdown;
sigset_t signalSet;

double getTime(struct timeval currentTime) {
    double current = 1000 * (currentTime.tv_sec) + (currentTime.tv_usec) / 1000.0;
    double initial = 1000 * (emulationStartTime.tv_sec) + (emulationStartTime.tv_usec) / 1000.0;
    return (current - initial);
}

void printStats() {
    fprintf(stdout, "Statistics:\n\n");

    if(0 == totalPackets) {
        fprintf(stdout, "\taverage packet inter-arrival time = %.6g, no packet arrived at this facility\n", totalIntervalTime/1000.0);
    }
    else {
        double avgIntervalTime = totalIntervalTime/totalPackets;
        fprintf(stdout, "\taverage packet inter-arrival time = %.6g\n", avgIntervalTime/1000.0);
    }

    if (0 == processedPackets) {  
        fprintf(stdout, "\tNo completed packets at this facility\n\n");
    }
    else {
        double avgServiceTime = totalServiceTime/processedPackets;
        fprintf(stdout, "\taverage packet service time = %.6g\n\n", avgServiceTime/1000.0);
    }

    fprintf(stdout, "\taverage number of packets in Q1 = %.6g\n", totalTimeInQ1/getTime(emulationEndTime));
    fprintf(stdout, "\taverage number of packets in Q2 = %.6g\n", totalTimeInQ2/getTime(emulationEndTime));
    fprintf(stdout, "\taverage number of packets in S1 = %.6g\n", totalTimeInS1/getTime(emulationEndTime));
    fprintf(stdout, "\taverage number of packets in S2 = %.6g\n\n", totalTimeInS2/getTime(emulationEndTime));


    double avgTimeInSystem = totalTimeInSystem/processedPackets;
    double avgTimeInSystemSq = totalTimeInSystemSq/processedPackets;
    double variance = avgTimeInSystemSq - pow(avgTimeInSystem, 2);
    double std;
    std = sqrt(variance);
    if (processedPackets == 0) {  
        fprintf(stdout, "\taverage time a packet spent in system = %.6g, no completed packets at this facility\n", avgTimeInSystem/1000.0);
        fprintf(stdout, "\tstandard deviation for time spent in system = %.6g, no completed packets at this facility\n", std/1000.0);
        fprintf(stdout, "\n");
    }
    else {
        fprintf(stdout, "\taverage time a packet spent in system = %.6g\n", avgTimeInSystem/1000.0);
        fprintf(stdout, "\tstandard deviation for time spent in system = %.6g\n", std/1000.0);
        fprintf(stdout, "\n");
    }


    
    if(0 == tokenCounter) { 
        fprintf(stdout, "\tno tokens arrived at this facility\n");
    }
    else {
        double tokenDropProbability = (1.0 * droppedTokens)/tokenCounter;
        fprintf(stdout, "\ttoken drop probability = %.6g\n", tokenDropProbability);
    }


    if(0 == totalPackets) {
        fprintf(stdout, "\tno packets arrived at this facility\n");
    }
    else {
        double packetDropProbability = (1.0 * droppedPackets)/totalPackets;
        fprintf(stdout, "\tpacket drop probability = %.6g\n", packetDropProbability);
    }

    fprintf(stdout, "\n");
}

void* ctrlRoutine() {
    struct timeval currentTime;
    while (1) {
        int signal;
        sigwait(&signalSet, &signal);
        pthread_mutex_lock(&m);
        fprintf(stdout, "SIGINT caught, no new packets or tokens will be allowed\n");
        isShutdown = true;
        pthread_cancel(packetThread);
        pthread_cancel(tokenThread);
        pthread_cond_broadcast(&cv);
        My402Packet* newPacket = NULL;
        while (My402ListLength(&q1) != 0) {
            newPacket = My402ListFirst(&q1)->obj;
            My402ListUnlink(&q1, My402ListFirst(&q1));
            gettimeofday(&currentTime, NULL);
            fprintf(stdout, "%012.3fms: p%d removed from Q1\n", getTime(currentTime), newPacket->id);
        }
        while (My402ListLength(&q2) != 0) {
            newPacket = My402ListFirst(&q2)->obj;
            My402ListUnlink(&q2, My402ListFirst(&q2));
            gettimeofday(&currentTime, NULL);
            fprintf(stdout, "%012.3fms: p%d removed from Q2\n", getTime(currentTime), newPacket->id);
        }
        pthread_mutex_unlock(&m);
    }
    return (0);
}

void* serverRoutine(void *arg) {
    struct timeval currentTime;
    while(1) { /*doubt*/
        pthread_mutex_lock(&m);
        while(tokenThreadRunning && My402ListEmpty(&q2) && !isShutdown) 
            pthread_cond_wait(&cv,&m);

        if(My402ListEmpty(&q2)) {
            pthread_mutex_unlock(&m);
            break;
        }

        My402ListElem* firstElem = My402ListFirst(&q2);
        My402Packet* newPacket = (My402Packet*)firstElem->obj;
        My402ListUnlink(&q2, firstElem);
        gettimeofday(&currentTime, NULL);
        newPacket->q2ExitTime = getTime(currentTime);
        gettimeofday(&currentTime, NULL);
        newPacket->serviceStartTime = getTime(currentTime);
        fprintf(stdout, "%012.3fms: p%d leaves Q2, time in Q2 = %.3fms\n", 
        newPacket->q2ExitTime, newPacket->id, (newPacket->q2ExitTime - newPacket->q2EnterTime));
        fprintf(stdout, "%012.3fms: p%d begins service at S%d, requesting %dms of service\n",
        newPacket->serviceStartTime, newPacket->id, (int)arg, newPacket->serviceTime);
        pthread_mutex_unlock(&m);
        usleep(newPacket->serviceTime * 1000);
        gettimeofday(&currentTime, NULL);
        newPacket->serviceEndTime = getTime(currentTime);
        fprintf(stdout, "%012.3fms: p%d departs from S%d, service time = %.3fms, time in system = %.3fms\n", newPacket->serviceEndTime, newPacket->id, (int)arg, (newPacket->serviceEndTime - newPacket->serviceStartTime), (newPacket->serviceEndTime - newPacket->arrivalTime));

        totalTimeInQ1 += (newPacket->q1ExitTime - newPacket->q1EnterTime);
        totalTimeInQ2 += (newPacket->q2ExitTime - newPacket->q2EnterTime);
        totalServiceTime += (newPacket->serviceEndTime - newPacket->serviceStartTime);
        
        if((int)arg == 1){
            totalTimeInS1 += (newPacket->serviceEndTime - newPacket->serviceStartTime);
        }
        else{
            totalTimeInS2 += (newPacket->serviceEndTime - newPacket->serviceStartTime);
        }

        totalTimeInSystem += (newPacket->serviceEndTime - newPacket->arrivalTime);
        totalTimeInSystemSq += pow(newPacket->serviceEndTime - newPacket->arrivalTime, 2);
        processedPackets += 1;
    }

    /*Thread Termination conditions*/
    pthread_exit(NULL);
}

void* tokenRoutine() {
    /*evaluate time*/
    double interArrivalTime = (1/tokenRate) * 1000.0;
    struct timeval currentTime;
    int tokenIndex = 0;
    while((true == packetThreadRunning) || 0 < (My402ListLength(&q1))) {
        usleep(interArrivalTime * 1000);
        pthread_mutex_lock(&m);
        gettimeofday(&currentTime, NULL);
        tokenIndex += 1;
        availableTokens += 1;
        if(availableTokens > bucketSize) {
            droppedTokens += 1;
            fprintf(stdout, "%012.3fms: token t%d arrives, dropped\n", getTime(currentTime), tokenIndex);
        } else {
            fprintf(stdout, "%012.3fms: token t%d arrives, token bucket now has %d token(s)\n", getTime(currentTime), tokenIndex, availableTokens);
        }

        if(!My402ListEmpty(&q1)) {
            My402ListElem* head =My402ListFirst(&q1);
            My402Packet* headPacket = (My402Packet*)head->obj;
            if(q1.num_members > 0 && availableTokens >= headPacket->tokensNeeded) {
                My402ListUnlink(&q1, head);
                gettimeofday(&currentTime, NULL);
                headPacket->q1ExitTime = getTime(currentTime);
                availableTokens -= headPacket->tokensNeeded;
                fprintf(stdout, "%012.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has %d token(s)\n", headPacket->q1ExitTime, headPacket->id, (headPacket->q1ExitTime - headPacket->q1EnterTime), availableTokens);
                
                My402ListAppend(&q2, headPacket);
                gettimeofday(&currentTime, NULL);
                headPacket->q2EnterTime = getTime(currentTime);
                fprintf(stdout, "%012.3fms: p%d enters Q2\n", headPacket->q2EnterTime, headPacket->id);
                if(My402ListLength(&q2) == 1) {
                    pthread_cond_broadcast(&cv);
                }
            }
        }
        tokenCounter += 1;
        pthread_mutex_unlock(&m);
    }

    
    pthread_mutex_lock(&m);
    tokenThreadRunning = false;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&m);
    pthread_exit(NULL);

} 

char* trimSpaces(char input[]) {
    int i = 0;
    int j = 0;
    char *str = (char*)malloc(strlen(input));
    strcpy(str, input);
    while(str[i] == '\t' || str[i] == ' ' || str[i] == '\n') {
        i++;
    }
    while('\0' != str[i+j]) {
        str[j] = str[i+j];
        j++;
    }
    str[j] = '\0';
    i = -1;
    j = 0;
    while('\0' != str[j]) {
        if(str[j] != '\t' && str[j] != ' ' && str[j] != '\n')
            i = j;
        j++;
    }
    str[i + 1] = '\0';
    return str;
}

bool isNumeric(const char* str) {
    if ((NULL == str) || isspace(*str) || ('\0' == *str)) {
        return false;
    }
    char* integerP = NULL;
    strtod(str, &integerP);
    return ('\0' == *integerP);
}



void* packetRoutine() {
    int packetIndex = 0;
    double prevPacketTime = 0;
    struct timeval currentTime;

    if(!isDeterministic) {
        FILE* filePtr = fopen(tsFile , "r");
        struct stat dir;
        char fileBuff[1024];
        if (NULL == filePtr) {
            fprintf(stderr, "[INPUT FILE: %s] %s\n", tsFile, strerror(errno));
            exit(false);
        } else {
            fseek(filePtr, 0, SEEK_END);
            if (ftell(filePtr) == 0) {
                fprintf(stderr, "[INVALID] Input file is empty\n");
                exit(false);
            }
            rewind(filePtr);
        }
        if(0 == stat(tsFile, &dir)) {
            if (S_ISDIR(dir.st_mode) != 0) {
                fprintf(stderr, "[INVALID] Input is a directory and not a file.\n");
                exit(false);
            }
        }
        fgets(fileBuff, sizeof(fileBuff), filePtr);
        if (fileBuff[strlen(fileBuff) - 1] == '\n') {
            fileBuff[strlen(fileBuff) - 1] = '\0';
        }
        if (false == isNumeric(fileBuff)) {
            fprintf(stderr, "[INVALID] The number of packets should be numeric.\n");
            exit(false);
        }
        numPackets = atoi(fileBuff);
        packetIndex += 1;
        while(fgets(fileBuff, sizeof(fileBuff), filePtr)) {
            if (fileBuff[strlen(fileBuff) - 1] == '\n') {
                fileBuff[strlen(fileBuff) - 1] = '\0';
            }
            if ('\0' == fileBuff[0]) {
                fprintf(stderr, "[INVALID] Line number %d is empty.\n", packetIndex);
                exit(false);
            }
            if (strlen(fileBuff) > 1023) {
                fprintf(stderr, "[INVALID] Line length exceeds the allowed limit.\n");
                exit(FALSE);
            }
            if (strcmp(fileBuff, trimSpaces(fileBuff)) != 0) {
                fprintf(stderr, "[INVALID] Leading/trailing spaces detected in line number: %d of ts file\n", packetIndex);
                exit(FALSE);
            }
            char *token = strtok(fileBuff, "   ");
            My402Packet* newPacket = (My402Packet*)malloc(sizeof(My402Packet));
            newPacket->id = packetIndex;
            if ((false == isNumeric(token)) || (atoi(token) < 0)) {
                fprintf(stderr, "[INVALID] The interArrival time for packet %d on line %d is Invalid.\n", packetIndex, packetIndex);
                
                exit(false);
            }
            newPacket->interArrivalTime = atof(token);
            token = strtok(NULL, "   ");
            if ((false == isNumeric(token)) || (atoi(token) < 0)) {
                fprintf(stderr, "[INVALID] Tokens required for packet %d on line %d is not valid.\n", packetIndex, packetIndex);
            
                exit(false);
            }
            newPacket->tokensNeeded = atoi(token);
            token = strtok(NULL, "   ");
            if ((false == isNumeric(token)) || (atoi(token) < 0)) {
                fprintf(stderr, "[INVALID] Service time for packet %d on line %d is invalid\n", packetIndex, packetIndex);
      
                exit(false);
            }
            newPacket->serviceTime = atof(token);
            
            usleep(newPacket->interArrivalTime * 1000);
            gettimeofday(&currentTime, NULL);
            newPacket->arrivalTime = getTime(currentTime);
            if(newPacket->tokensNeeded <= bucketSize) {
                fprintf(stdout, "%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms\n",
                                                                                        newPacket->arrivalTime,
                                                                                        newPacket->id,
                                                                                        newPacket->tokensNeeded,
                                                                                        newPacket->arrivalTime - prevPacketTime);
                My402ListAppend(&q1, newPacket);
                totalIntervalTime += newPacket->arrivalTime - prevPacketTime;
                prevPacketTime = newPacket->arrivalTime;
                gettimeofday(&currentTime, NULL);
                newPacket->q1EnterTime = getTime(currentTime);
                fprintf(stdout, "%012.3fms: p%d enters Q1\n", newPacket->q1EnterTime, newPacket->id);

                if((My402ListLength(&q1) == 1) && (newPacket->tokensNeeded <= availableTokens)) {
                    availableTokens -= newPacket->tokensNeeded;
                    My402ListUnlink(&q1, My402ListFirst(&q1));
                    gettimeofday(&currentTime, NULL) ;
                    newPacket->q1ExitTime = getTime(currentTime);
                    fprintf(stdout, "%012.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has %d token\n",
                                                                                    getTime(currentTime),
                                                                                    newPacket->id,
                                                                                    newPacket->q1ExitTime - newPacket->q1EnterTime,
                                                                                    availableTokens);
                    My402ListAppend(&q2, newPacket);
                    gettimeofday(&currentTime, NULL) ;
                    newPacket->q2EnterTime = getTime(currentTime);
                    fprintf(stdout, "%012.3fms: p%d enters Q2\n", newPacket->q2EnterTime, newPacket->id);
                    gettimeofday(&currentTime, NULL);
                    ((My402Packet*)My402ListLast(&q2)->obj)->q2EnterTime = getTime(currentTime);
                    if (My402ListLength(&q2) == 1) {
                        pthread_cond_broadcast(&cv);
                    }
                }
            } else {
                totalIntervalTime += newPacket->arrivalTime - prevPacketTime;
                prevPacketTime = newPacket->arrivalTime;
                fprintf(stdout, "%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms, dropped\n",
                                                                            newPacket->arrivalTime,
                                                                            newPacket->id,
                                                                            newPacket->tokensNeeded,
                                                                            newPacket->interArrivalTime);
                droppedPackets += 1;
                free(newPacket);
            }
            totalPackets += 1;
            pthread_mutex_unlock(&m);
            packetIndex += 1;
        }
        if(numPackets != (packetIndex - 1)) {
            fprintf(stderr, "No of packets in ts file are invalid\n");
            exit(FALSE);
        }          
    } else {
        while(packetIndex < numPackets) {
            My402Packet* newPacket = (My402Packet*)malloc(sizeof(My402Packet));
            newPacket->id = packetIndex + 1;
            usleep((1000.0/packetRate)*1000);
            gettimeofday(&currentTime, NULL);
            newPacket->arrivalTime = getTime(currentTime);
            newPacket->interArrivalTime = newPacket->arrivalTime - prevPacketTime;
            newPacket->tokensNeeded = tokensPerPacket;
            newPacket->serviceTime = 1000.0/serverRate;
            pthread_mutex_lock(&m);
            if(newPacket->tokensNeeded <= bucketSize) {
                fprintf(stdout, "%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms\n",
                                                                                        newPacket->arrivalTime,
                                                                                        newPacket->id,
                                                                                        newPacket->tokensNeeded,
                                                                                        newPacket->interArrivalTime);
                My402ListAppend(&q1, newPacket);
                totalIntervalTime += newPacket->arrivalTime - prevPacketTime;
                prevPacketTime = newPacket->arrivalTime;
                gettimeofday(&currentTime, NULL);
                newPacket->q1EnterTime = getTime(currentTime);
                fprintf(stdout, "%012.3fms: p%d enters Q1\n", newPacket->q1EnterTime, newPacket->id);

                if ((My402ListLength(&q1) == 1) && (newPacket->tokensNeeded <= availableTokens)){
                    availableTokens -= newPacket->tokensNeeded;
                    My402ListUnlink(&q1, My402ListFirst(&q1));
                    gettimeofday(&currentTime, NULL);
                    newPacket->q1ExitTime = getTime(currentTime);
                    fprintf(stdout, "%012.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has %d token\n",
                                                                            getTime(currentTime),
                                                                            newPacket->id,
                                                                            newPacket->q1ExitTime - newPacket->q1EnterTime,
                                                                            availableTokens);
                    My402ListAppend(&q2, newPacket);
                    gettimeofday(&currentTime, NULL);
                    ((My402Packet*)My402ListLast(&q2)->obj)->q2EnterTime = getTime(currentTime);
                    fprintf(stdout, "%012.3fms: p%d enters Q2\n", ((My402Packet*)My402ListLast(&q2)->obj)->q2EnterTime,
                                                                ((My402Packet*)My402ListLast(&q2)->obj)->id);
                    if (My402ListLength(&q2) == 1) {
                        pthread_cond_broadcast(&cv);
                    }
                }
            } else {
                totalIntervalTime += newPacket->arrivalTime - prevPacketTime;
                prevPacketTime = newPacket->arrivalTime;
                fprintf(stdout, "%012.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms, dropped\n",
                                                                            newPacket->arrivalTime,
                                                                            newPacket->id,
                                                                            newPacket->tokensNeeded,
                                                                            newPacket->interArrivalTime);
                droppedPackets += 1;
                free(newPacket);
            }
            totalPackets += 1;
            pthread_mutex_unlock(&m);
            packetIndex += 1;
        }
    }
    pthread_mutex_lock(&m);
    packetThreadRunning = false;
    pthread_mutex_unlock(&m);
    pthread_exit(NULL);

}

void validateFileParams(char* tsFile) {
    struct stat dirCheck;
    stat(tsFile, &dirCheck);
    FILE* filePtr = fopen(tsFile , "r");  
    if (S_ISDIR(dirCheck.st_mode)) {
        fprintf(stderr, "[Error] The file %s is a directory.\n", tsFile);
        exit(false);             
    }
    if (NULL == filePtr) { 
        fprintf(stderr, "Entered tsFile is : %s. Error: %s", tsFile, strerror(errno));
        exit(false);
    }
    char fileBuff[1024]; 
    if (NULL == fgets(fileBuff, sizeof(fileBuff), filePtr)) {
        fprintf(stderr, "[Error] The file is empty.\n");
        exit(false);
    }
}

void validateCommand(int argc, char *argv[]) {
    /* Expected format: warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile] */
    if (argc > 15) { 
        fprintf(stderr, "[Malformed command] Usage: warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
        exit(false);
    }

    for(int i=1; i<argc; i=i+2) {
        /*validate lambda value*/
        if(0 == strcmp("-lambda", argv[i])) {
            if(argc > i+1) {
                if(argv[i+1][0] == '-') {
                    fprintf(stderr, "[Incorrect input type] Lambda value cannot be negative\n");
                    exit(false);
                }
                if(1 != sscanf(argv[i+1], "%lf", &packetRate)) {
                    fprintf(stderr, "[Malformed command] Value of lambda is incorrect. Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                    exit(false);
                }
                packetRate = atof(argv[i + 1]);
                if (packetRate < 0.1) {
                    packetRate = 0.1;
                }

            } else {
                fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                exit(false);
            }
            
        }

        /*validate mu value*/
        else if(0 == strcmp("-mu", argv[i])) {
            if(argc > i+1) {
                if(argv[i+1][0] == '-') {
                    fprintf(stderr, "[Incorrect input type] mu value cannot be negative\n");
                    exit(false);
                }
                if(1 != sscanf(argv[i+1], "%lf", &serverRate)) {
                    fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                    exit(false);
                }
                serverRate = atof(argv[i + 1]);
                if (serverRate < 0.1) {
                    serverRate = 0.1;
                }
            } else {
                fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                exit(false);
            }
        }

        /*validate r value*/
        else if(0 == strcmp("-r", argv[i])) {
            if(argc > i+1) {
                if(argv[i+1][0] == '-') {
                    fprintf(stderr, "[Incorrect input type] r value cannot be negative\n");
                    exit(false);
                }
                if(1 != sscanf(argv[i+1], "%lf", &tokenRate)) {
                    fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                    exit(false);
                }
                if (tokenRate < 0.1) {
                    tokenRate = 0.1;
                }
            } else {
                fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                exit(false);
            }
        }

        /*validate B value*/
        else if(0 == strcmp("-B", argv[i])) {
            if(argc > i+1) {
                if(argv[i+1][0] == '-') {
                    fprintf(stderr, "[Incorrect input type] B value cannot be negative\n");
                    exit(false);
                }
                if(1 != sscanf(argv[i+1], "%i", &bucketSize)) {
                    fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                    exit(false);
                }
                if(bucketSize > MAX_BPN) {
                    fprintf(stderr, "[Incorrect input type] B value cannot be greater than 2147483647");
                    exit(false);
                }
            } else {
                fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                exit(false);
            }
        }

        /*validate P value*/
        else if(0 == strcmp("-P", argv[i])) {
            if(argc > i+1) {
                if(argv[i+1][0] == '-') {
                    fprintf(stderr, "[Incorrect input type] P value cannot be negative\n");
                    exit(false);
                }
                if(1 != sscanf(argv[i+1], "%i", &tokensPerPacket)) {
                    fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                    exit(false);
                }
                if(tokensPerPacket > MAX_BPN) {
                    fprintf(stderr, "[Incorrect input type] P value cannot be greater than 2147483647");
                    exit(false);
                }
            } else {
                fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                exit(false);
            }
        }

        /*validate n value*/
        else if(0 == strcmp("-n", argv[i])) {
            if(argc > i+1) {
                if(argv[i+1][0] == '-') {
                    fprintf(stderr, "[Incorrect input type] n value cannot be negative\n");
                    exit(false);
                }
                if(1 != sscanf(argv[i+1], "%i", &numPackets)) {
                    fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                    exit(false);
                }
                if(numPackets > MAX_BPN) {
                    fprintf(stderr, "[Incorrect input type] n value cannot be greater than 2147483647");
                    exit(false);
                }
            } else {
                fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                exit(false);
            }
        }

        /*validate t value*/
        else if(0 == strcmp("-t", argv[i])) {
            if(argc > i+1) {
                isDeterministic = false;
                tsFile = (char*)malloc(strlen(argv[i + 1]) + 1);
                strcpy(tsFile, argv[i+1]);
            } else {
                fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
                exit(false);
            }
        }

        else {
            fprintf(stderr, "[Malformed command] Usage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n");
            exit(false);
        }
    }
}

void setDefaults() {
    tokenRate = 1.5;
    bucketSize = 10;
    numPackets = 20;
    tokensPerPacket = 3; 
    packetRate = 1.0;
    serverRate = 0.35;    
}

void setGlobals() {
    isDeterministic = true;
    availableTokens = 0;
    totalTimeInQ1 = 0;
    totalTimeInQ2 = 0;
    totalTimeInS1 = 0;
    totalTimeInS2 = 0;
    totalServiceTime = 0;
    totalTimeInSystem = 0;
    totalTimeInSystemSq = 0;
    totalIntervalTime = 0;
    
    tokenCounter = 0;
    droppedTokens = 0;

    totalPackets = 0;
    droppedPackets = 0;
    processedPackets = 0;

    packetThreadRunning = false;
    tokenThreadRunning = false;
    isShutdown = false;
}

int main(int argc, char *argv[]) {
    setDefaults();
    setGlobals();
    validateCommand(argc, argv);
    memset(&q1, 0, sizeof(My402List));
    memset(&q2, 0, sizeof(My402List));
    My402ListInit(&q1);
    My402ListInit(&q2);
    
    fprintf(stdout, "Emulation Parameters:\n");
    fprintf(stdout, "\tnumber to arrive = %i\n", numPackets);
    if (isDeterministic) { 
        fprintf(stdout, "\tlambda = %.6g\n", packetRate);
        fprintf(stdout, "\tmu = %.6g\n", serverRate);
        fprintf(stdout, "\tr = %.6g\n", tokenRate);
        fprintf(stdout, "\tB = %i\n", bucketSize);
        fprintf(stdout, "\tP = %i\n", tokensPerPacket);
    }else { 
        fprintf(stdout, "\tr = %.6g\n", tokenRate); 
        fprintf(stdout, "\tB = %i\n", bucketSize);
        fprintf(stdout, "\ttsfile = %s\n", tsFile);
    }
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    sigprocmask(SIG_BLOCK, &signalSet, 0);

    gettimeofday(&emulationStartTime, NULL); 
    fprintf(stdout, "%012.3fms: Start Emulation\n", getTime(emulationStartTime));
    pthread_create(&cntrlCThread, NULL, ctrlRoutine, 0);
    pthread_create(&packetThread, NULL, packetRoutine, 0);
    packetThreadRunning = true;
    pthread_create(&tokenThread, NULL, tokenRoutine, 0);
    tokenThreadRunning = true;
    pthread_create(&serverOneThread, NULL, serverRoutine, (void *)1);
    pthread_create(&serverTwoThread, NULL, serverRoutine, (void *)2);
    pthread_join(packetThread, NULL);
    pthread_join(tokenThread, NULL);
    pthread_join(serverOneThread, NULL);
    pthread_join(serverTwoThread, NULL);
    pthread_cancel(cntrlCThread);
    gettimeofday(&emulationEndTime, NULL);
    fprintf(stdout, "%012.3fms: emulation ends\n\n", getTime(emulationEndTime));
    printStats(); 
    return(0);
}