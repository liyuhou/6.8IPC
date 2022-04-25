#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#define LEN (64+16) // 16=10+6,ie. digits of timestamp sec+usec. A maximum of 63 characters can be entered
int ppipe[2],cpipe[2];
int pid;
int isReady=0;
char line[LEN]={0}, pRecvBuf[LEN]={0}, pMessage[LEN]={0};
struct timeval t;
long timeStamp[6]={0};

struct itimerval timer;

void timeStampFormatEncode(struct timeval val, char* p)
{
    char sec[11]={0};//10+1
    char usec[7]={0};//6+1
    int i=0;
    sprintf(sec,"%ld",val.tv_sec);
    sprintf(usec,"%ld",val.tv_usec);
    for(i=0;i<10-strlen(sec);i++){
        p[i]='0';
    }
    strcat(p,sec);
    for(i=10;i<16-strlen(usec);i++){
        p[i]='0';
    }
    strcat(p,usec);
}

void timeStampFormatDecode(struct timeval* val, char* p)
{
    char sec[11]={0};
    char usec[7]={0};
    int len=strlen(p);
    int i=0;
    strncpy(sec,p,10);
    strncpy(usec,p+10,6);
    val->tv_sec = atol(sec);
    val->tv_usec = atol(usec);
    for(i=0;i<len-16;i++){
        p[i]=p[16+i];
    }
    for(;i<LEN;i++) p[i]=0;
    
}

void stringtoUpper(char* p, int len)
{
    for(int i=0;i<len;i++){
        if(p[i]>='a' && p[i]<='z'){
            p[i]-=32;
        }
    }
}

void phandler(int sig, siginfo_t *siginfo, void *context)
{
    printf("in phandler\n");
    memset(&timer,0,sizeof(timer));//shut down timer
    setitimer(ITIMER_REAL,&timer,NULL);
    read(cpipe[0],pRecvBuf,LEN);
    printf("message from child is: %s\n",pRecvBuf);
    timeStampFormatDecode(&t,pRecvBuf);
    timeStamp[2]=t.tv_sec;
    timeStamp[3]=t.tv_usec;
    stringtoUpper(line,strlen(line));
    gettimeofday(&t,NULL);
    timeStamp[4]=t.tv_sec;
    timeStamp[5]=t.tv_usec;
    printf("time cost from parent to child is %ld seconds, %ld useconds\n",timeStamp[2]-timeStamp[0],timeStamp[3]-timeStamp[1]);
    printf("time cost from child to parent is %ld seconds, %ld useconds\n",timeStamp[4]-timeStamp[2],timeStamp[5]-timeStamp[3]);
    if(strcmp(line,pRecvBuf) == 0) {
        isReady = 1;
        printf("the return message is right\nReady for new line\n");
    }
    else {
        isReady=0;
        printf("the return message is wrong\n");
    }
    memset(pRecvBuf,0,strlen(pRecvBuf));
}

void timerHandler(int sig)
{
    printf("in timerHandler:\n");
    gettimeofday(&t,NULL);
    timeStamp[0] = t.tv_sec;
    timeStamp[1] = t.tv_usec;
    memset(pMessage,0,sizeof(pMessage));
    timeStampFormatEncode(t,pMessage);
    strcat(pMessage,line);
    write(ppipe[1],pMessage,strlen(pMessage));
    printf("resend pMessage = %s\n",pMessage);

}

int parent()
{
    printf("parent %d running\n", getpid());
    struct sigaction act;
    signal(SIGALRM, timerHandler);
    memset(&act,0,sizeof(act));
    memset(&timer,0,sizeof(timer));
    act.sa_sigaction = &phandler;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &act, NULL);
    close(ppipe[0]);
    close(cpipe[1]);
    while(1){
        printf("parent %d: input a line: \n", getpid());
        memset(pMessage,0,sizeof(pMessage));
        memset(line,0,sizeof(line));
        fgets(line, LEN, stdin);
        if(line[64-1]!='\n' && line[64-1]!=0){
            printf("the input line exceeds the capability!\ninput again\n");
            continue;
        }
        line[strlen(line)-1]=0;//delete '\n'
        gettimeofday(&t,NULL);
        timeStamp[0] = t.tv_sec;
        timeStamp[1] = t.tv_usec;
        timeStampFormatEncode(t,pMessage);
        strcat(pMessage,line);
        write(ppipe[1],pMessage,strlen(pMessage));
        printf("parent %d write to pipe: %s\n", getpid(),pMessage);
        printf("parent %d send signal 10 to %d\n",getpid(), pid);
        isReady=0;
        kill(pid, SIGUSR1);
        timer.it_interval.tv_usec = 50000;
        timer.it_value.tv_usec = 50000;
        setitimer(ITIMER_REAL,&timer,NULL);
        printf("parent waiting for reply\n");
        while(!isReady);
    }
}

void chandler(int sig, siginfo_t *siginfo, void *context)
{
    char cMessage[LEN] = {0}, cRecvBuf[LEN]={0};
    //int pid=getpid();
    //printf("\nchild %d got an interrupt sig=%d\n", pid, sig);
    read(ppipe[0],cRecvBuf, LEN);
    //printf("child %d get a message = %s\n",pid, cRecvBuf);
    timeStampFormatDecode(&t, cRecvBuf);
    stringtoUpper(cRecvBuf,strlen(cRecvBuf));
    gettimeofday(&t,NULL);
    timeStampFormatEncode(t,cMessage);
    strcat(cMessage,cRecvBuf);
    printf("child reply with: %s\n",cMessage);                   
    write(cpipe[1],cMessage,strlen(cMessage));
    kill(getppid(),SIGUSR1);
    //memset(cRecvBuf,0,sizeof(cRecvBuf));
}

int child()
{
    int parent = getppid();
    struct sigaction act;
    memset(&act,0,sizeof(act));
    act.sa_sigaction = &chandler;  
    act.sa_flags = SA_SIGINFO;
    printf("child %d running\n", getpid());
    close(ppipe[1]);
    close(cpipe[0]);
    sigaction(SIGUSR1, &act, NULL);
    while(1);
}

int main()
{
    pipe(ppipe);
    pipe(cpipe);
    pid=fork();
    if(pid){
        parent();
    }
    else{
        child();
    }
}