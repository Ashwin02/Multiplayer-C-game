/*
Ashwin Patil
test_prg.cpp
*/
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>
#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <mqueue.h>
#include <sstream>
#include <utility>
#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <cstring>


using namespace std;

#include "goldchase.h"
#include "Map.h"


struct GameBoard {
  int rows; //4 bytes
  int cols; //4 bytes
  pid_t pid[5];
  int deamonID;
  unsigned char players;
  unsigned char map[0];
};

Map *mapPtr = NULL;
GameBoard* goldmap;
// GameBoard* demonGoldmap;
mqd_t queueFD[5];
pid_t pidPlayer;
int location;
int rows;
int cols;
int player2Rows;
int player2Cols;
int totalCols;
int totalRows;
// int clientRows;
// int clientCols;
sem_t *my_sem_ptr;
sem_t *client_sem_ptr;
unsigned char currentPlayer;
bool cFlag = FALSE;
int debugFD;
int new_sockfd;
int pipefd[2];
int sockfd; //file descriptor for the socket
unsigned char *localCopy;
//unsigned char *localCopyClientMap;
unsigned char *clientMap;
//GameBoard* clientGoldmap;

int mapSize = 0; // stores rows * cols
 int newSize;

mqd_t readqueue_fd; //message queue file descriptor
mqd_t writequeue_fd;
string mq_name="/G_PLAYER";

// READ template
template<typename T>
int READ(int fd, T* obj_ptr, int count)
{
  char* addr=(char*)obj_ptr;
  //loop. Read repeatedly until count bytes are read in
  int bytesRead = 0;
  int totalBytesRead = 0;
  while (count > 0)
  {
	bytesRead = read(fd, addr + totalBytesRead, count);

    if (bytesRead == -1)
    {
      if (errno != EINTR)
      {
        return -1;
      }
    }
	else
	{
		totalBytesRead += bytesRead;
		count -= totalBytesRead;
	}
  }
  return totalBytesRead;
}
// Write template

template<typename T>
int WRITE(int fd, T* obj_ptr, int count)
{
  char* addr=(char*)obj_ptr;
  //loop. Write repeatedly until count bytes are written out
  int bytesWritten = 0;
  int totalBytesWritten = 0;
  while (count > 0)
  {

	bytesWritten = write(fd, addr + totalBytesWritten, count);
    if (bytesWritten == -1)
    {
      if (errno != EINTR)
      {
        return -1;
      }
    }
	else
	{
		totalBytesWritten += bytesWritten;
		count -= totalBytesWritten;
	}
  }
  return totalBytesWritten;
}


/* Function Declerations */
int giveCurrentPlayer()
{
    int current = 0;
    for(int i = 0;i<5;i++)
    {
      if(getpid() == goldmap->pid[i]){
        current = i;
        return current;
      }
    }
}

void refreshSignal(pid_t a[])
{
  for(int i = 0; i<5 ; i++)
  {
    if(a[i] != 0)
    {
    //  kill(a[i],SIGUSR1);
    }
  }
}

void handle_interrupt(int x)
{
  {
    if(mapPtr != NULL)
    {
      mapPtr->drawMap();
    }

  }
}

void read_message(int)
{
  struct sigevent mq_notification_event;
  mq_notification_event.sigev_notify=SIGEV_SIGNAL;
  mq_notification_event.sigev_signo=SIGUSR2;
  mq_notify(readqueue_fd, &mq_notification_event);

  //read a message
  int err;
  char msg[121];
  memset(msg, 0, 121);//set all characters to '\0'
  while((err=mq_receive(readqueue_fd, msg, 120, NULL))!=-1)
  {

    mapPtr->postNotice(msg);
    memset(msg, 0, 121);//set all characters to '\0'
  }
  //we exit while-loop when mq_receive returns -1
  //if errno==EAGAIN that is normal: there is no message waiting
  if(errno!=EAGAIN)
  {
    perror("mq_receive");
    exit(1);
  }

}

void cleanUp(int)
{
   cFlag = TRUE;
   for (int i=0;i<5;i++)
   {
    if(goldmap->pid[i]!=0)
    {
       kill(goldmap->pid[i], SIGUSR1);
    }
  }
   return;
}

void write_message(int players,string msg)
{
  const char *pt = msg.c_str();

  if((writequeue_fd = mq_open(mq_name.c_str(), O_WRONLY | O_NONBLOCK)) == -1)
  {
    perror("mq_open");
    exit(1);
  }

  char msg_text[121];
  memset(msg_text,0,121);
  strncpy(msg_text,pt,120);

  if(mq_send(writequeue_fd,msg_text,strlen(msg_text),0)==-1)
  {
    perror("mq_send");
    exit(1);
  }
  mq_close(writequeue_fd);

}

void initQueue(int players)
{
  if(players == G_PLR0) mq_name = "/G_PLAYER1";
  else if(players == G_PLR1) mq_name = "/G_PLAYER2";
  else if(players == G_PLR2) mq_name = "/G_PLAYER3";
  else if(players == G_PLR3) mq_name = "/G_PLAYER4";
  else if(players == G_PLR4) mq_name = "/G_PLAYER5";

  struct sigaction action_to_take;
  action_to_take.sa_handler = read_message;
  sigemptyset(&action_to_take.sa_mask);
  action_to_take.sa_flags=0;
  sigaction(SIGUSR2, &action_to_take, NULL);

  struct mq_attr mq_attributes;
  mq_attributes.mq_flags=0;
  mq_attributes.mq_maxmsg=10;
  mq_attributes.mq_msgsize=120;

  if((readqueue_fd=mq_open(mq_name.c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
          S_IRUSR|S_IWUSR, &mq_attributes))==-1)
  {
    perror("mq_open");
    exit(1);
  }

  struct sigevent mq_notification_event;
  mq_notification_event.sigev_notify=SIGEV_SIGNAL;
  mq_notification_event.sigev_signo=SIGUSR2;
  mq_notify(readqueue_fd, &mq_notification_event);
//  mq_close(writequeue_fd);
}

void broadcastMessage(string queue , string msg)
{
  const char *pt = msg.c_str();

  if((writequeue_fd = mq_open(queue.c_str(), O_WRONLY | O_NONBLOCK)) == -1)
  {
    perror("mq_open");
    exit(1);
  }

  char msg_text[121];
  memset(msg_text,0,121);
  strncpy(msg_text,pt,120);

  if(mq_send(writequeue_fd,msg_text,strlen(msg_text),0)==-1)
  {
    perror("mq_send");
    exit(1);
  }
  mq_close(writequeue_fd);
}

void broadcast(int player , string msg)
{
  if(player & G_PLR0)
  broadcastMessage("/G_PLAYER1" , msg);
  if(player & G_PLR1)
  broadcastMessage("/G_PLAYER2" , msg);
  if(player & G_PLR2)
  broadcastMessage("/G_PLAYER3" , msg);
  if(player & G_PLR3)
  broadcastMessage("/G_PLAYER4" , msg);
  if(player & G_PLR4)
  broadcastMessage("/G_PLAYER5" , msg);
  mapPtr->drawMap();
}

void declareWinner(int player , int currentP)
{
  string msg;
  int all;
  if(player == 0)
  {
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {
        kill(goldmap->pid[i], SIGUSR1);
      }
    }
    msg = "Player "+ to_string(player+1) +" Wins";
    all = goldmap->players;
    all &= ~currentP;
    broadcast(all , msg);
  }
  else if(player == 1)
  {
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {
        kill(goldmap->pid[i], SIGUSR1);
      }
    }
    msg = "Player "+ to_string(player+1) +" Wins";
    all = goldmap->players;
    all &= ~currentP;
    broadcast(all , msg);
  }
  else if(player == 2)
  {
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {
       kill(goldmap->pid[i], SIGUSR1);
      }
    }
    msg = "Player "+ to_string(player+1) +" Wins";
    all = goldmap->players;
    all &= ~currentP;
    broadcast(all , msg);
  }
  else if(player == 3)
  {
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {
        kill(goldmap->pid[i], SIGUSR1);
      }
    }
    msg = "Player "+ to_string(player+1) +" Wins";
    all = goldmap->players;
    all &= ~currentP;
    broadcast(all , msg);
  }
  else if(player == 4)
  {
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {
        kill(goldmap->pid[i], SIGUSR1);
      }
    }
    msg = "Player "+ to_string(player+1) +" Wins";
    all = goldmap->players;
    all &= ~currentP;
    broadcast(all , msg);
  }
}

void SIGHUPHandle(int)
{
  // debugFD = open("/home/ashwin/611/Project3/Project2/debugFD",O_WRONLY);
  // write(debugFD,"sighup",sizeof("sighup"));
  // close(debugFD);
  unsigned char writeByte;
  writeByte = G_SOCKPLR;
  for(int i=0;i<5;i++)
  {
    if(goldmap->pid[i] != 0 && i == pidPlayer)
    {
      switch(i)
      {
        case 0:
        writeByte |= G_PLR0;
        break;
        case 1:
        writeByte |= G_PLR1;
        break;
        case 2:
        writeByte |= G_PLR2;
        break;
        case 3:
        writeByte |= G_PLR3;
        break;
        case 4:
        writeByte |= G_PLR4;
        break;
      }
    }

  }

  WRITE(new_sockfd,&writeByte,sizeof(writeByte));

  if(writeByte == G_SOCKPLR)
  {
     sem_unlink("/ASPgoldchase");
     shm_unlink("/ashwinsGoldMem");
  }
  goldmap->players |= writeByte;
}

void SIGHUPHandleClient(int)
{
  // int fifover = open("/home/ashwin/Project2/myfile",O_WRONLY);
  // write(fifover,"\nsighupcli",sizeof("\nsighupcli"));
//  close(fifover);
  unsigned char writeByteClient;
  writeByteClient = G_SOCKPLR;

  for(int i=0;i<5;i++)
  {
    if(goldmap->pid[i] != 0 )
    {
      switch(i)
      {
        case 0:
        writeByteClient |= G_PLR0;
        break;
        case 1:
        writeByteClient |= G_PLR1;
        break;
        case 2:
        writeByteClient |= G_PLR2;
        break;
        case 3:
        writeByteClient |= G_PLR3;
        break;
        case 4:
        writeByteClient |= G_PLR4;
        break;
      }
    }

  }


  WRITE(sockfd,&writeByteClient,sizeof(writeByteClient));

  if(writeByteClient == G_SOCKPLR)
  {

     sem_unlink("/ASPgoldchase");
     shm_unlink("/ashwinsGoldMem");
  }
  goldmap->players |= writeByteClient;
}

void SIGUSR1Handle(int)
{
  // debugFD = open("/home/ashwin/611/Project3/Project2/debugFD",O_WRONLY);
  //  write(debugFD,"sigusr1",sizeof("sigusr1"));

  vector< pair<short,unsigned char> > pvec;
  for(short i=0; i<(goldmap->rows*goldmap->cols); ++i)
  {
    if(goldmap->map[i] != localCopy[i])
    {
      pair<short,unsigned char> aPair;
      aPair.first=i;
      aPair.second=goldmap->map[i];
      pvec.push_back(aPair);
      localCopy[i]=goldmap->map[i];
    }
  }

  short vecSize = pvec.size();
  unsigned char byte = 0;

  if(vecSize > 0)
  {
    WRITE(new_sockfd,&byte,sizeof(byte));
    WRITE(new_sockfd,&vecSize,sizeof(vecSize));

    for(int k=0 ; k < vecSize ; k++)
    {

      WRITE(new_sockfd,&pvec[k].first,sizeof(pvec[k].first));
      WRITE(new_sockfd,&pvec[k].second,sizeof(pvec[k].second));

    }
  }
}

void SIGUSR1HandleClient(int)
{
  // int fifover = open("/home/ashwin/Project2/myfile",O_WRONLY);
  // write(fifover,"\nsigusr1cli",sizeof("\nsigusr1cli"));


  vector< pair<short,unsigned char> > pvecCli;
  for(short i=0; i<(goldmap->rows*goldmap->cols); ++i)
  {
    if(goldmap->map[i]!=clientMap[i])
    {
      pair<short,unsigned char> aPair;
      aPair.first=i;
      aPair.second=goldmap->map[i];
      pvecCli.push_back(aPair);
      clientMap[i]=goldmap->map[i];
    }
  }

  short vecSizeCli = pvecCli.size();
  unsigned char byte = 0;

  if(vecSizeCli > 0)
  {
    WRITE(sockfd,&byte,sizeof(byte));
    WRITE(sockfd,&vecSizeCli,sizeof(vecSizeCli));

    for(int k=0 ; k < vecSizeCli ; k++)
    {

      WRITE(sockfd,&pvecCli[k].first,sizeof(pvecCli[k].first));
      WRITE(sockfd,&pvecCli[k].second,sizeof(pvecCli[k].second));

    }
  }

}


void SIGUSR2HandleServer(int)
{
  int err;
  unsigned char writeByte;
  short len;

  char msg[121];
  memset(msg, 0, 121);

  writeByte = G_SOCKMSG;

  for(int i=0;i<5;i++)
  {
    if((err=mq_receive(queueFD[i], msg, 120, NULL))!=-1)
    {

      struct sigevent mq_notification_event;
      mq_notification_event.sigev_notify=SIGEV_SIGNAL;
      mq_notification_event.sigev_signo=SIGUSR2;
      mq_notify(queueFD[i], &mq_notification_event);

      if(goldmap->pid[i] != 0)
      {
        switch(i)
        {
          case 0:
          writeByte |= G_PLR0;
          break;
          case 1:
          writeByte |= G_PLR1;
          break;
          case 2:
          writeByte |= G_PLR2;
          break;
          case 3:
          writeByte |= G_PLR3;
          break;
          case 4:
          writeByte |= G_PLR4;
          break;
      }
    }

      WRITE(new_sockfd,&writeByte,sizeof(writeByte));

      len = strlen(msg);

      WRITE(new_sockfd,&len,sizeof(len));
      for(short j=0;j<len;j++)
      {
        WRITE(new_sockfd,&msg[j],sizeof(msg[j]));
      }
    }
  }
}


void SIGUSR2HandleClient(int)
{
  int err;
  unsigned char writeByteClient;
  short len;
  char msg[121];
  memset(msg, 0, 121);

  writeByteClient = G_SOCKMSG;


  for(int i=0;i<5;i++)
  {
    if((err=mq_receive(queueFD[i], msg, 120, NULL))!=-1)
    {
        struct sigevent mq_notification_event;
        mq_notification_event.sigev_notify=SIGEV_SIGNAL;
        mq_notification_event.sigev_signo=SIGUSR2;
        mq_notify(queueFD[i], &mq_notification_event);

        if(goldmap->pid[i] != 0)
        {
          switch(i)
          {
            case 0:
            writeByteClient |= G_PLR0;
            break;
            case 1:
            writeByteClient |= G_PLR1;
            break;
            case 2:
            writeByteClient |= G_PLR2;
            break;
            case 3:
            writeByteClient |= G_PLR3;
            break;
            case 4:
            writeByteClient |= G_PLR4;
            break;
          }
        }

        WRITE(sockfd,&writeByteClient,sizeof(writeByteClient));

        len = strlen(msg);

        WRITE(sockfd,&len,sizeof(len));
        for(int j=0;j<len;j++)
        {
          WRITE(sockfd,&msg[j],sizeof(msg[j]));
        }

  }
}
}

void ServerStartup()
{
  if(fork() > 0)
  {
    // write(debugFD,"fork\n",sizeof("fork\n"));
    return;
  }
  if(fork()>0) //if I'm the parent
  {
    exit(0);   //then exit. This leaves only the child
  }


  if(setsid()==-1)
  {
    exit(1);
  }

  for(int i=0; i< sysconf(_SC_OPEN_MAX); ++i)
  {
      close(i);
  }


  open("/dev/null", O_RDWR); //fd 0
  open("/dev/null", O_RDWR); //fd 1
  open("/dev/null", O_RDWR); //fd 2
  umask(0);
  chdir("/");


 // Server socket

 int status; //for error checking
 //change this # between 2000-65k before using
 const char* portno="48000";
 struct addrinfo hints;
 memset(&hints, 0, sizeof(hints)); //zero out everything in structure
 hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
 hints.ai_socktype=SOCK_STREAM; // TCP stream sockets
 hints.ai_flags=AI_PASSIVE; //file in the IP of the server for me

 struct addrinfo *servinfo;
 if((status=getaddrinfo(NULL, portno, &hints, &servinfo))==-1)
 {
   fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
   exit(1);
 }
 sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

 /*avoid "Address already in use" error*/

 int yes=1;
 if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))==-1)
 {
   perror("setsockopt");
   exit(1);
 }

 //We need to "bind" the socket to the port number so that the kernel
 //can match an incoming packet on a port to the proper process
 if((status=bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
 {
   perror("bind");
   exit(1);
 }
 //when done, release dynamically allocated memory
 freeaddrinfo(servinfo);

 if(listen(sockfd,1)==-1)
 {
   perror("listen");
   exit(1);
 }

 printf("Blocking, waiting for client to connect\n");

 struct sockaddr_in client_addr;
 socklen_t clientSize=sizeof(client_addr);


 do{


    new_sockfd=accept(sockfd, (struct sockaddr*) &client_addr, &clientSize);
  }while(new_sockfd == -1 && errno == EINTR);

  // Started trapping the signals

  struct sigaction SIGHUP_handler;
  SIGHUP_handler.sa_handler=SIGHUPHandle;
  sigemptyset(&SIGHUP_handler.sa_mask);
  SIGHUP_handler.sa_flags=0;
  SIGHUP_handler.sa_restorer=NULL;
  sigaction(SIGHUP, &SIGHUP_handler, NULL);


  struct sigaction SIGUSR1_handler;
  SIGUSR1_handler.sa_handler=SIGUSR1Handle;
  sigemptyset(&SIGUSR1_handler.sa_mask);
  SIGUSR1_handler.sa_flags=0;
  SIGUSR1_handler.sa_restorer=NULL;
  sigaction(SIGUSR1, &SIGUSR1_handler, NULL);

  struct sigaction SIGUSR2_handler;
  SIGUSR2_handler.sa_handler=SIGUSR2HandleServer;
  sigemptyset(&SIGUSR2_handler.sa_mask);
  SIGUSR2_handler.sa_flags=0;
  SIGUSR2_handler.sa_restorer=NULL;
  sigaction(SIGUSR2, &SIGUSR2_handler, NULL);

  struct mq_attr mq_attributes;
  mq_attributes.mq_flags=0;
  mq_attributes.mq_maxmsg=10;
  mq_attributes.mq_msgsize=120;


 //  Step 2 for G_SOCKPLR
 int serverRows , serverCols;

 int sharedMemory;
 sharedMemory = shm_open("/ashwinsGoldMem",O_RDWR, S_IRUSR | S_IWUSR);

 READ(sharedMemory, &serverRows, sizeof(serverRows));
 READ(sharedMemory, &serverCols, sizeof(serverCols));

 goldmap=(GameBoard*) mmap(NULL,sizeof(GameBoard)+(serverRows*serverCols),PROT_WRITE,MAP_SHARED,sharedMemory,0);
 goldmap->deamonID = getpid();
 localCopy = new unsigned char[goldmap->rows * goldmap->cols];

 for(int i=0;i<(serverRows*serverCols);i++)
 {

   localCopy[i]=goldmap->map[i];
 }


    WRITE(new_sockfd,&serverRows,sizeof(serverRows));
    WRITE(new_sockfd,&serverCols,sizeof(serverCols));


    for(int i=0;i<(serverRows*serverCols);i++)
    {
        unsigned char tempB = localCopy[i];
        WRITE(new_sockfd,&tempB,sizeof(tempB));
    }

    unsigned char tempG_SOCKPLR;
    tempG_SOCKPLR = G_SOCKPLR;
    for(int i=0;i<5;i++)
    {
      if(goldmap->pid[i] != 0)
      {
        switch(i)
        {
          case 0:
          tempG_SOCKPLR |= G_PLR0;
          break;
          case 1:
          tempG_SOCKPLR |= G_PLR1;
          break;
          case 2:
          tempG_SOCKPLR |= G_PLR2;
          break;
          case 3:
          tempG_SOCKPLR |= G_PLR3;
          break;
          case 4:
          tempG_SOCKPLR |= G_PLR4;
          break;
        }
      }
    }


    WRITE(new_sockfd,&tempG_SOCKPLR,sizeof(tempG_SOCKPLR));

while(1)
{
  unsigned char byte;
  short vecSize;
  short loc;
  unsigned char player;

   //int debugFD = open("/home/ashwin/611/Project3/Project2/debugFD",O_WRONLY);
  // write(debugFD,"\ninwhile",sizeof("\ninwhile"));

  READ(new_sockfd,&byte,1);

  // if((byte & G_PLR0) == 0)
  // {
  //   write(debugFD,"\nplr1",sizeof("\nplr1"));
  // }
  //  if((byte == G_PLR1) == 0)
  // {
  //   write(debugFD,"\nplr2",sizeof("\nplr2"));
  // }
   if((byte == G_PLR2) == 0)
  {
  //  write(debugFD,"\nplr3",sizeof("\nplr3"));
  }

  // if(byte != 0)
  // {
  //   // write(debugFD,"\nbyte!0",sizeof("\nbyte!0"));
  // }
  // Case 1 player joins
 if (byte & G_SOCKPLR)
 {
    goldmap->players |= byte;
//   write(debugFD,"\ninifnewplayer",sizeof("\ninifnewplayer"));
   unsigned char playerBit[5] = {G_PLR0,G_PLR1,G_PLR2,G_PLR3,G_PLR4};
   string msgqueue[5] = {"/G_PLAYER1","/G_PLAYER2","/G_PLAYER3","/G_PLAYER4","/G_PLAYER5"};
  // string msgQ;
   for(int i=0;i<5;i++)
   {
    //  write(debugFD,"\ninif1",sizeof("\ninif1"));
     if((byte & playerBit[i]) && goldmap->pid[i] == 0)
     //if((byte & playerBit[i] != 0) && goldmap->pid[i] == 0)
     {
       //1. create and open players message queue
    //   write(debugFD,"\ninif",sizeof("\ninif"));

      //  stringstream name;
      //  name<<"/G_PLAYER"<<i+1;
      //  mq_name = name.str();

    //    msgQ = msgqueue[i].c_str();

       if((queueFD[i]=mq_open(msgqueue[i].c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
               S_IRUSR|S_IWUSR, &mq_attributes))==-1)
       {
         perror("mq_open");
         exit(1);
       }

       struct sigevent mq_notification_event;
       mq_notification_event.sigev_notify=SIGEV_SIGNAL;
       mq_notification_event.sigev_signo=SIGUSR2;
       mq_notify(queueFD[i], &mq_notification_event);


       //2. shared memory id = pid of demon

         goldmap->pid[i] =  getpid();
     }
     else if(!(byte & playerBit[i]) && goldmap->pid[i] != 0)
     {
       // 2.1
       mq_close(queueFD[i]);
       mq_unlink(msgqueue[i].c_str());
       // 2.2
       goldmap->pid[i] = 0;
     }
     if(byte == G_SOCKPLR)
     {
       // sem_post(my_sem_ptr);
       sem_close(my_sem_ptr);
       sem_unlink("/ASPgoldchase");
       shm_unlink("/ashwinsGoldMem");

     }
     if(tempG_SOCKPLR != G_SOCKPLR)
     {
      // write(debugFD,"\ninifkill",sizeof("\ninifkill"));
      // kill(goldmap->pid[i],SIGUSR1);
         kill(goldmap->deamonID,SIGUSR1);
     }
   }
 }

    else if(byte & G_SOCKMSG)
    {
      unsigned char recipent;
      short len;
      stringstream rec;
      char msg_text[121];
      memset(msg_text,0,121);

      for(int i=0;i<5;i++)
      {
        if(goldmap->pid[i] != 0)
        {
          switch(i)
          {
            case 0:
            mq_name = "/G_PLAYER1";
            break;
            case 1:
            mq_name = "/G_PLAYER2";
            break;
            case 2:
            mq_name = "/G_PLAYER3";
            break;
            case 3:
            mq_name = "/G_PLAYER4";
            break;
            case 4:
            mq_name = "/G_PLAYER5";
            break;
          }
        }
      }

            READ(sockfd,&len,sizeof(len));

            if((writequeue_fd = mq_open(mq_name.c_str(), O_WRONLY | O_NONBLOCK)) == -1)
            {
              perror("mq_open");
              exit(1);
            }

            for(short j=0;j<len;j++)
            {

              READ(sockfd,&msg_text,sizeof(msg_text));

              if(mq_send(writequeue_fd,msg_text,strlen(msg_text),0)==-1)
              {
                perror("mq_send");
                exit(1);
              }
              mq_close(writequeue_fd);
            }
        }


   else if(byte == 0)
   {
    // write(debugFD,"\nbyte0",sizeof("\nbyte0"));
     READ(new_sockfd,&vecSize,sizeof(vecSize));
    if(vecSize > 0)
    {
      if(vecSize == 1)
      {
      //  write(debugFD,"\nrightvecSize",sizeof("\nrightvecSize"));
      }
      for(short j=0;j<vecSize;j++)
      {
      //  write(debugFD,"\ninvecsize",sizeof("\ninvecsize"));

        READ(new_sockfd,&loc,sizeof(loc));
      //  write(debugFD,"\n1",sizeof("\n1"));
        READ(new_sockfd,&player,sizeof(player));
      //  write(debugFD,&player,sizeof(player));
      //  write(debugFD,"\n2",sizeof("\n2"));
        localCopy[loc] = player;
        goldmap->map[loc] = player;
      }
      for(int i=0;i<5;i++)
      {
        if(goldmap->pid[i] != 0 && goldmap->pid[i] != getpid())
        {
        //  kill(goldmap->pid[i],SIGUSR1);
      //      write(debugFD,"\ncallkill",sizeof("\ncallkill"));
            kill(goldmap->pid[i], SIGUSR1);
        }
      }
    }
  }
}
}


void clientStartup(string ip)
 {
   unsigned char clientG_SOCKPLR;

  //  int fifover = open("/home/ashwin/Project2/myfile",O_WRONLY);
  //  write(fifover,"\nincli1",sizeof("\nincli1"));

   pipe(pipefd);

  if(fork()>0)//parent;
  {

  //  write(fifover,"infork()",sizeof("infork()"));
    close(pipefd[1]); //close write, parent only needs read
    int val;
    READ(pipefd[0], &val, sizeof(val));
    if(val==1)
    {

  //  write(fifover,"firstfork()",sizeof("firstfork()"));
    write(1, "Success!\n", sizeof("Success!\n"));

    }
    else
    {
  //    write(fifover,"fail",sizeof("fail"));
      write(1, "Failure!\n", sizeof("Failure!\n"));
    }
    return;
  }
  if(fork() > 0)
  {
  //  write(fifover,"firstfork()2",sizeof("firstfork()2"));
    exit(0);
  }

  if(setsid()==-1)
    exit(1);
  for(int i=0; i< sysconf(_SC_OPEN_MAX); ++i)
  {
    if(i!=pipefd[1])//close everything, except write
      close(i);
  }
  open("/dev/null", O_RDWR); //fd 0
  open("/dev/null", O_RDWR); //fd 1
  open("/dev/null", O_RDWR); //fd 2
  umask(0);
  chdir("/");

// write(fifover,"\nafterdemon",sizeof("\nafterdemon"));
  // Started trapping the signals

  struct sigaction SIGHUP_handler_client;
  SIGHUP_handler_client.sa_handler=SIGHUPHandleClient;
  sigemptyset(&SIGHUP_handler_client.sa_mask);
  SIGHUP_handler_client.sa_flags=0;
  SIGHUP_handler_client.sa_restorer=NULL;
  sigaction(SIGHUP, &SIGHUP_handler_client, NULL);


  struct sigaction SIGUSR1_handler_client;
  SIGUSR1_handler_client.sa_handler=SIGUSR1HandleClient;
  sigemptyset(&SIGUSR1_handler_client.sa_mask);
  SIGUSR1_handler_client.sa_flags=0;
  SIGUSR1_handler_client.sa_restorer=NULL;
  sigaction(SIGUSR1, &SIGUSR1_handler_client, NULL);

  struct sigaction SIGUSR2_handler_client;
  SIGUSR2_handler_client.sa_handler=SIGUSR2HandleClient;
  sigemptyset(&SIGUSR2_handler_client.sa_mask);
  SIGUSR2_handler_client.sa_flags=0;
  SIGUSR2_handler_client.sa_restorer=NULL;
  sigaction(SIGUSR2, &SIGUSR2_handler_client, NULL);

  struct mq_attr mq_attributes;
  mq_attributes.mq_flags=0;
  mq_attributes.mq_maxmsg=10;
  mq_attributes.mq_msgsize=120;

  /* int sockfd; //file descriptor for the socket
   int status; //for error checking

   //change this # between 2000-65k before using
   const char* portno="46000"; */

   struct addrinfo hints;
   const char* portno="48000";
   memset(&hints, 0, sizeof(hints)); //zero out everything in structure
   hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
   hints.ai_socktype=SOCK_STREAM; // TCP stream sockets

   struct addrinfo *servinfo;
   int status;
   //instead of "localhost", it could by any domain name
   if((status=getaddrinfo(ip.c_str(), portno, &hints, &servinfo))==-1)
   {
     fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
     exit(1);
   }

  if((sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1)
  {
    perror("socket");
    exit(1);
  }

   if((status=connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
   {
     perror("connect");
     exit(1);
   }
   //release the information allocated by getaddrinfo()
   freeaddrinfo(servinfo);
    // new_sockfd = sockfd;
  int clientRows;
  int clientCols;
  // int fifover = open("/home/ashwin/Project2/myfile",O_WRONLY);
  // write(fifover,"\naftersock",sizeof("\naftersock"));

  unsigned char playerBit[5] = {G_PLR0,G_PLR1,G_PLR2,G_PLR3,G_PLR4};
  string msgqueue[5] = {"/G_PLAYER1","/G_PLAYER2","/G_PLAYER3","/G_PLAYER4","/G_PLAYER5"};
//  string msgQ;


   READ(sockfd, &clientRows , sizeof(int));
   READ(sockfd, &clientCols , sizeof(int));

      clientMap = new unsigned char[(clientRows * clientCols)];

   for(int i=0;i<(clientRows*clientCols);i++)
   {
     unsigned char ash;
     READ(sockfd,&ash,sizeof(ash));
     clientMap[i] = ash;
   }



    my_sem_ptr = sem_open("/ASPgoldchase",O_CREAT|O_EXCL, S_IRUSR|S_IWUSR,1);

    if(my_sem_ptr==SEM_FAILED)
         {

           if(errno!=EEXIST)
           {
             perror("Error");
             exit(1);
           }
         }

    if(my_sem_ptr != SEM_FAILED)
      {
          int fd;
          fd = shm_open("/ashwinsGoldMem",O_RDWR|O_CREAT,S_IRUSR|S_IWUSR);
          if(fd == -1)
          {
          //  write(fifover,"error_shm\n",sizeof("error_shm\n"));
          }

        if((ftruncate(fd,clientRows*clientCols)) == -1)
        {
        //  write(fifover,"error_ftruncate\n",sizeof("error_ftruncate\n"));
        }
          goldmap = (GameBoard*) mmap(NULL, sizeof(GameBoard) + (clientRows*clientCols),PROT_WRITE, MAP_SHARED, fd, 0);

          goldmap->rows = clientRows;
          goldmap->cols = clientCols;
          goldmap->deamonID = getpid();

          unsigned char clientPlr;
          READ(sockfd,&clientPlr,sizeof(clientPlr));
          goldmap->players |= clientPlr;
          for(int i=0;i<5;i++)
          {
            if((clientPlr & playerBit[i]) && goldmap->pid[i] == 0)
            {
           //   msgQ = msgqueue[i].c_str();
              goldmap->pid[i] =  getpid();
              if((queueFD[i]=mq_open(msgqueue[i].c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
                      S_IRUSR|S_IWUSR, &mq_attributes))==-1)
              {
                perror("mq_open");
                exit(1);
              }

              struct sigevent mq_notification_event;
              mq_notification_event.sigev_notify=SIGEV_SIGNAL;
              mq_notification_event.sigev_signo=SIGUSR2;
              mq_notify(queueFD[i], &mq_notification_event);

           }
          }

          for(int i=0;i<(clientRows*clientCols);i++)
          {
            unsigned char copyB = clientMap[i];
            goldmap->map[i] = copyB;
          }

          int val = 1;
          WRITE(pipefd[1],&val,sizeof(val));



      while(1)
      {

        unsigned char byte;
        short vecSize;
        short loc;
        unsigned char player;


        READ(sockfd,&byte,1);

        // Case 1 player joins
        if (byte & G_SOCKPLR)
        {

          for(int i=0;i<5;i++)
          {
            if((byte & playerBit[i]) && goldmap->pid[i] == 0)
            {
              //1. create and open players message queue


              if((queueFD[i]=mq_open(msgqueue[i].c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
                      S_IRUSR|S_IWUSR, &mq_attributes))==-1)
              {

                perror("mq_open");
                exit(1);
              }

              struct sigevent mq_notification_event;
              mq_notification_event.sigev_notify=SIGEV_SIGNAL;
              mq_notification_event.sigev_signo=SIGUSR2;
              mq_notify(queueFD[i], &mq_notification_event);

              //2. shared memory id = pid of demon

                goldmap->pid[i] =  getpid();
            }
            else if(!(byte & playerBit[i]) && goldmap->pid[i] != 0)
            {
              // 2.1

              mq_close(queueFD[i]);
              mq_unlink(msgqueue[i].c_str());


              // 2.2
              goldmap->pid[i] = 0;
            }
            if(byte == G_SOCKPLR)
            {
              // sem_post(my_sem_ptr);
              sem_close(my_sem_ptr);
              sem_unlink("/ASPgoldchase");
              shm_unlink("/ashwinsGoldMem");


            }
            if(clientPlr != G_SOCKPLR)
            {
              kill(goldmap->deamonID, SIGUSR1);
            }
          }
        }

        // case 2 Socket message

        if(byte & G_SOCKMSG)
        {
          unsigned char recipent;
          short len;
          stringstream rec;
          char msg_text[121];
          memset(msg_text,0,121);

          for(int i=0;i<5;i++)
          {

            if(goldmap->pid[i] != 0)
            {
              switch(i)
              {
                case 0:
                mq_name = "/G_PLAYER1";
                break;
                case 1:
                mq_name = "/G_PLAYER2";
                break;
                case 3:
                mq_name = "/G_PLAYER3";
                break;
                case 4:
                mq_name = "/G_PLAYER4";
                break;
                case 5:
                mq_name = "/G_PLAYER5";
                break;
              }
            }
          }

            READ(sockfd,&len,sizeof(len));

            if((writequeue_fd = mq_open(mq_name.c_str(), O_WRONLY | O_NONBLOCK)) == -1)
            {
              perror("mq_open");
              exit(1);
            }

            for(short j=0;j<len;j++)
            {

              READ(sockfd,&msg_text,sizeof(msg_text));

              if(mq_send(writequeue_fd,msg_text,strlen(msg_text),0)==-1)
              {
                perror("mq_send");
                exit(1);
              }
              mq_close(writequeue_fd);
            }


        }



          // Case 3 Socket map. Refreshing map

          else if(byte == 0)
          {
            // int fifover = open("/home/ashwin/Project2/myfile",O_WRONLY);
            // write(fifover,"\nbyte0",sizeof("\nbyte0"));

            READ(sockfd,&vecSize,sizeof(vecSize));
            if(vecSize > 0)
            {
              for(short j=0;j<vecSize;j++)
              {
                READ(sockfd,&loc,sizeof(loc));
                READ(sockfd,&player,sizeof(player));
                clientMap[loc] = player;
                goldmap->map[loc] = player;
              }
              for(int i=0 ; i<5 ; i++)
              {
                if(goldmap->pid[i] != 0 && goldmap->pid[i] != getpid())
                {
                //  kill(goldmap->pid[i],SIGUSR1);
                  kill(goldmap->pid[i],SIGUSR1);
                }
              }

          } //close vecSize -1
        } // close byte==0 - 1
      } //byte & G_SOCKPLR - 1
    } //while(1) - 1
  } //sem failed - 1



int main(int argc , char *argv[])
{
  string line;
  int gold;
  int key, pRow, pCol, pLoc;
  totalCols = cols - 1;
  totalRows = rows - 1;
  bool Flag = FALSE;
  string ip;
  string msg;
  pid_t myPid;
  // write(fifover,"\nin main 1",sizeof("\nin main 1"));
  if(argc == 2 )
  {
    int sharedMemory;
    if(sharedMemory = shm_open("/ashwinsGoldMem",O_RDWR, S_IRUSR | S_IWUSR)==-1)
    {
      clientStartup(argv[1]);
    }

  }

  srand(time(NULL));
  // write(fifover,"\nin main 2",sizeof("\nin main 2"));


    struct sigaction sig_handler;
    sig_handler.sa_handler=handle_interrupt;
    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_flags=0;
    sig_handler.sa_restorer=NULL;
    sigaction(SIGUSR1, &sig_handler, NULL);


    struct sigaction exit_handler;
    exit_handler.sa_handler=cleanUp;
    sigemptyset(&exit_handler.sa_mask);
    exit_handler.sa_flags=0;
    sigaction(SIGINT, &exit_handler, NULL);
    sigaction(SIGTERM, &exit_handler, NULL);

  if(argc != 2)
  {
    ifstream myFile ("mymap.txt");
    if (myFile.is_open())
    {

      getline(myFile , line); // Ignoring the first line
      gold = atoi(line.c_str());

      while(!myFile.eof())
      {
        getline(myFile , line);
        if(!myFile.eof())
        {
          ++rows;
        }
       if(rows <= 2)
       {
         cols = line.length();
       }
      }

      myFile.close();
    }
    else cout << "Unable to open file";
  }



  /* Shared Memory stuff */
  mapSize = rows * cols;
  my_sem_ptr= sem_open( "/ASPgoldchase",  O_CREAT | O_EXCL, S_IRUSR | S_IWUSR,1);
//  sem_wait(my_sem_ptr);
  // write(fifover,"\nshould be here 1",sizeof("\nshould be here 1"));

  if(my_sem_ptr==SEM_FAILED)
       {

         if(errno!=EEXIST)
         {

           perror("Error");
           exit(1);
         }
       }

 if(my_sem_ptr != SEM_FAILED)
   {

    //  write(fifover,"\nshould be here 2",sizeof("\nshould be here 2"));
     sem_wait(my_sem_ptr);
     int fd = shm_open("/ashwinsGoldMem",O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);

// Using frtruncate to grow the shared memory
    newSize = ftruncate(fd,rows*cols+sizeof(GameBoard));

    // write(fifover,"\nshould be here 3",sizeof("\nshould be here 3"));
//  goldmap= (GameBoard*)mmap(NULL, rows*cols, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    goldmap= (GameBoard*)mmap(NULL, sizeof(GameBoard)+(rows*cols), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    goldmap->rows=rows;
    goldmap->cols=cols;
    goldmap->deamonID = 0;
    // write(fifover,"\nshould be here 4",sizeof("\nshould be here 4"));


  // Reading the text file again

int array_size = mapSize;
char * array = new char[array_size+1];
int pos = 0;
char ch;


//if(argc != 2)
{
    ifstream fin("mymap.txt");
    if(fin.is_open())
  {

   getline(fin , line); // ignore first line
    // loop will run until eof occurs
    while(fin.get(ch))
    {

      if(ch != '\n')
      {
        array[pos] = ch;
        ++pos;
      }

    }
    array[pos+1] = '\0'; // placing terminating characters
    fin.close();
  }
}
//converting each character in the file to bits
  for(int i = 0; array[i] != '\0'; i++)
  {
   if(array[i] == '*')   goldmap->map[i] = G_WALL;
   else if(array[i] == ' ') goldmap->map[i] = 0;
   else if(array[i] == 'G') goldmap->map[i] = G_GOLD;
   else if(array[i] == 'F') goldmap->map[i] = G_FOOL;
  }


// Placing gold on map

 for(int i=0; i < gold ; i++)
 {
   do{

     location = rand() % mapSize;
   }
   while(goldmap->map[location] != 0);

     if(i == 0)
     {
       goldmap->map[location] = G_GOLD;
     }
     else
     {
       goldmap->map[location] = G_FOOL;
     }
   }



// Placing player 1 on the map

do{

  location = rand() % mapSize;
}
while(goldmap->map[location] != 0);
goldmap->map[location] = G_PLR0;
goldmap->players = G_PLR0; // Current player is playing
goldmap->pid[0] = getpid();
pidPlayer = 0;
currentPlayer = G_PLR0;
initQueue(G_PLR0);
for (int i=0;i<5;i++)
{
    if(goldmap->pid[i]!=0)
    {
       kill(goldmap->pid[i], SIGUSR1);
       kill(goldmap->deamonID, SIGUSR1);
    }
  }

sem_post(my_sem_ptr);

  }
//}// end of if (!SEM_FAILED) loop



else
{


  // write(fifover,"\nshould be here",sizeof("\nshould be here"));
  my_sem_ptr = sem_open("/ASPgoldchase",O_RDWR, S_IRUSR | S_IWUSR,1);

  //sem_wait(my_sem_ptr);

  int fd = shm_open("/ashwinsGoldMem",O_RDWR,S_IRUSR | S_IWUSR | S_IRWXU);

  // int player2Rows;
  // int player2Cols;



  read(fd, &player2Rows,sizeof(int));
  read(fd, &player2Cols,sizeof(int));

  rows = player2Rows;
  cols = player2Cols;
  totalCols = cols - 1;
  totalRows = rows - 1;

  goldmap = (GameBoard*)mmap(NULL, (player2Rows*player2Cols) + sizeof(GameBoard*), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

  //int tempMapSize = rows * cols;
  int tempMapSize = player2Rows * player2Cols;
  while(goldmap->map[location] != 0)
  {

    location = rand() % tempMapSize;
  }

  if((goldmap->players & G_PLR1) == 0)
  {
    goldmap->map[location] = G_PLR1;
    goldmap->players |= G_PLR1;
    goldmap->pid[1] = getpid();
    pidPlayer = 1;
    currentPlayer = G_PLR1;
    initQueue(G_PLR1);
  //  mapPtr-drawMap();
    //goldMine.drawMap();

  for (int i=0;i<5;i++)
  {
      if(goldmap->pid[i]!=0)
      {
          kill(goldmap->pid[i], SIGUSR1);
          kill(goldmap->deamonID, SIGUSR1);
      //   kill(goldmap->deamonID, SIGUSR1);
      }
    }
  }

  else if((goldmap->players & G_PLR2) == 0)
  {
    goldmap->map[location] = G_PLR2;
    goldmap->players |= G_PLR2;
    goldmap->pid[2] = getpid();
    pidPlayer = 2;
    currentPlayer = G_PLR2;
    initQueue(G_PLR2);
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {
         kill(goldmap->pid[i], SIGUSR1);
         kill(goldmap->deamonID, SIGUSR1);
      //   kill(goldmap->deamonID, SIGUSR1);
      }
    }

  }

  else if((goldmap->players & G_PLR3) == 0)
  {
    goldmap->map[location] = G_PLR3;
    goldmap->players |= G_PLR3;
    goldmap->pid[3] = getpid();
    pidPlayer = 3;
    currentPlayer = G_PLR3;

    initQueue(G_PLR3);
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {

         kill(goldmap->pid[i], SIGUSR1);
         kill(goldmap->deamonID, SIGUSR1);
      }
    }
  }
  else if((goldmap->players & G_PLR4) == 0)
  {
    goldmap->map[location] = G_PLR4;
    goldmap->players |= G_PLR4;
    goldmap->pid[4] = getpid();
    pidPlayer = 4;
    currentPlayer = G_PLR4;
    initQueue(G_PLR4);
    for (int i=0;i<5;i++)
    {
      if(goldmap->pid[i]!=0)
      {

        kill(goldmap->pid[i], SIGUSR1);
        kill(goldmap->deamonID, SIGUSR1);
      }
    }
  }

  else
  {

    cout << "Sorry ..!! The board is Full right now." << endl;
    exit(0);

  }


  sem_post(my_sem_ptr);

} // end of else loop


if(goldmap->deamonID == 0)
{

    ServerStartup();

}
else
{

  kill(goldmap->deamonID,SIGHUP);
}

// Loop to get the keyboard controls

Map goldMine((const unsigned char*)goldmap->map,goldmap->rows,goldmap->cols);
mapPtr = &goldMine;
goldMine.postNotice("Press q to quit...");
do{

  key = goldMine.getKey();

  if(key == 'l')
  {
    int pRow = location / cols; // Row of active player.
    int pCol = location % cols; // Column of active player.
    int pLoc = ((pRow*cols) + pCol);
    int moveRight = location + 1;
    // for (int i=0;i<5;i++)
    // {
    //   if(goldmap->pid[i]!=0)
    //   {
    //
    //      kill(goldmap->deamonID, SIGUSR1);
    //      kill(goldmap->pid[i], SIGUSR1);
    //   }
    // }

    if(pCol != totalCols)
    {
      if(goldmap->map[moveRight] & G_FOOL)
      {
        goldmap->map[moveRight] &= ~G_FOOL;
        goldMine.drawMap();
        goldMine.postNotice("You have found Fool's Gold");

      }
      else if(goldmap->map[moveRight] & G_GOLD)
      {
        goldMine.postNotice("You have found the Real Gold");
        Flag = TRUE;
        goldMine.drawMap();
        goldmap->map[moveRight] &= ~G_GOLD;


      }

      else if(goldmap->map[moveRight] != G_WALL && ((goldmap->map[moveRight]) & goldmap->players) == 0)
      {
        sem_wait(my_sem_ptr);
        goldmap->map[location] &= ~currentPlayer;
        goldmap->map[moveRight] = currentPlayer;

        for (int i=0;i<5;i++)
        {
            if(goldmap->pid[i]!=0)
            {
              kill(goldmap->pid[i], SIGUSR1);
              kill(goldmap->deamonID, SIGUSR1);
            }
        }



       if(Flag == TRUE && pCol != totalCols )
        {
          if(moveRight % goldmap->cols == (goldmap->cols) - 1)
          {
            goldMine.postNotice("You won ...!!!");
            key = 'Q';

            goldMine.drawMap();
            int winner = giveCurrentPlayer();
            declareWinner(winner,currentPlayer);

            goldMine.drawMap();
          }


        }
        goldmap->map[moveRight] = currentPlayer;
        goldmap->map[location] = 0;

        goldMine.drawMap();
        sem_post(my_sem_ptr);
        location = moveRight;
      }

    }
  }

// Moving Left.

  if(key == 'h')
  {
    pRow = location / cols; // Row of active player.
    pCol = location % cols; // Column of active player.
    pLoc = ((pRow*cols) + pCol);
    // for (int i=0;i<5;i++)
    // {
    //   if(goldmap->pid[i]!=0)
    //   {
    //      kill(goldmap->deamonID, SIGUSR1);
    //   //   kill(goldmap->pid[i], SIGUSR1);
    //   }
    // }
    int moveLeft = location - 1;

    if(pCol != 0)
    {
      if(goldmap->map[moveLeft] & G_FOOL)
      {
        goldmap->map[moveLeft] &= ~G_FOOL;
        //goldMine.drawMap();
        goldMine.postNotice("You have found Fool's Gold");
        goldMine.drawMap();

      }
      else if(goldmap->map[moveLeft] & G_GOLD)
      {
        goldMine.postNotice("You have found the Real Gold");
        Flag = TRUE;
      //  goldMine.drawMap();
        goldmap->map[moveLeft] &= ~G_GOLD;
        goldMine.drawMap();

      }
      else if(goldmap->map[moveLeft] != G_WALL && ((goldmap->map[moveLeft]) & goldmap->players) == 0)
      {
        sem_wait(my_sem_ptr);

        goldmap->map[location] &= ~currentPlayer;
        goldmap->map[moveLeft] = currentPlayer;

        for (int i=0;i<5;i++)
        {
          if(goldmap->pid[i]!=0)
          {

             kill(goldmap->pid[i], SIGUSR1);
             kill(goldmap->deamonID, SIGUSR1);
          }
        }

        if(Flag == TRUE && pCol != 0 )
         {

           if(moveLeft % goldmap->cols == 0)
           {
             goldMine.postNotice("You won ...!!!");

             key = 'Q';

              goldMine.drawMap();
             int winner = giveCurrentPlayer();

             declareWinner(winner,currentPlayer);

             goldMine.drawMap();
           }
         }


        goldmap->map[moveLeft] = currentPlayer;
        goldmap->map[location] = 0;

        goldMine.drawMap();
        sem_post(my_sem_ptr);
        location = moveLeft;
      }

  }
}

// Moving Down

  if(key == 'j')
  {
    int totalRows;
    pRow = location / cols; // Row of active player.
    pCol = location % cols; // Column of active player.
    pLoc = ((pRow*cols) + pCol);

  //  int moveDown = 0;
    int moveDown = location + cols;
    totalRows = rows - 1;
    if(pRow != totalRows)
    {
      if(goldmap->map[moveDown] & G_FOOL)
      {
        goldmap->map[moveDown] &= ~G_FOOL;

        goldMine.postNotice("You have found Fool's Gold");
        goldMine.drawMap();
      }
      else if(goldmap->map[moveDown] & G_GOLD)
      {
        goldMine.postNotice("You have found the Real Gold");
        Flag = TRUE;
        goldmap->map[moveDown] &= ~G_GOLD;
        goldMine.drawMap();


      }
      else if(goldmap->map[moveDown] != G_WALL && ((goldmap->map[moveDown]) & goldmap->players) == 0)
      {
        sem_wait(my_sem_ptr);
        goldmap->map[location] &= ~currentPlayer;
        goldmap->map[moveDown] = currentPlayer;

        for (int i=0;i<5;i++)
        {
          if(goldmap->pid[i]!=0)
          {

             kill(goldmap->pid[i], SIGUSR1);
             kill(goldmap->deamonID, SIGUSR1);
          }
        }

        if(Flag == TRUE && pRow != totalRows )
         {
           int temp = moveDown / goldmap->cols;
           if(temp % goldmap->cols == goldmap->rows-1)
           {
             goldMine.postNotice("You won ...!!!");
             key = 'Q';
             goldMine.drawMap();
             int winner = giveCurrentPlayer();
             goldMine.drawMap();
             declareWinner(winner,currentPlayer);
             goldMine.drawMap();
           }
         }

        goldmap->map[moveDown] = currentPlayer;
        goldmap->map[location] = 0;

        goldMine.drawMap();
        sem_post(my_sem_ptr);
        location = moveDown;
      }

    }
  }

// Moving Up

  if(key == 'k')
  {
    pRow = location / cols; // Row of active player.
    pCol = location % cols; // Column of active player.
    pLoc = ((pRow*cols) + pCol);
    // for (int i=0;i<5;i++)
    // {
    //   if(goldmap->pid[i]!=0)
    //   {
    //      kill(goldmap->deamonID, SIGUSR1);
    //   //   kill(goldmap->pid[i], SIGUSR1);
    //   }
    // }
    int moveUp = location - cols;
    if(pRow != 0)
    {
      if(goldmap->map[moveUp] & G_FOOL)
      {
        goldmap->map[moveUp] &= ~G_FOOL;

        goldMine.postNotice("You have found Fool's Gold");
        goldMine.drawMap();
      }
      else if(goldmap->map[moveUp] & G_GOLD)
      {
        goldMine.postNotice("You have found the Real Gold");
        Flag = TRUE;

        goldmap->map[moveUp] &= ~G_GOLD;
        goldMine.drawMap();
      }
      else if(goldmap->map[moveUp] != G_WALL && ((goldmap->map[moveUp]) & goldmap->players) == 0)
      {
        sem_wait(my_sem_ptr);
        goldmap->map[location] &= ~currentPlayer;
        goldmap->map[moveUp] = currentPlayer;

        for (int i=0;i<5;i++)
        {
          if(goldmap->pid[i]!=0)
          {

             kill(goldmap->pid[i], SIGUSR1);
             kill(goldmap->deamonID, SIGUSR1);
          }
        }

        if(Flag == TRUE && pRow != 0 )
         {
           int temp = moveUp / goldmap->cols;
           if(temp % goldmap->cols == 0)
           {
             goldMine.postNotice("You won ...!!!");
             key = 'Q';
             goldMine.drawMap();
             int winner = giveCurrentPlayer();
             goldMine.drawMap();
             declareWinner(winner,currentPlayer);
             goldMine.drawMap();
           }
         }
        goldmap->map[moveUp] = currentPlayer;
        goldmap->map[location] = 0;

          goldMine.drawMap();
        sem_post(my_sem_ptr);
        location = moveUp;
      }

    }
  }


// sending message

  if(key == 'm')
  {
    int tempKey;
    int current = giveCurrentPlayer();
    int receiver = goldMine.getPlayer(goldmap->players & ~currentPlayer);
    bool playerPresent = false;
    if(receiver == G_PLR0)
    {
      mq_name = "/G_PLAYER1";
      playerPresent = true;
    }
    else if(receiver == G_PLR1)
    {
     mq_name = "/G_PLAYER2";
     playerPresent = true;
   }
    else if(receiver == G_PLR2)
    {
      mq_name = "/G_PLAYER3";
      playerPresent = true;
    }
    else if(receiver == G_PLR3)
    {
      mq_name = "/G_PLAYER4";
      playerPresent = true;
    }
    else if(receiver == G_PLR4)
    {
      mq_name = "/G_PLAYER5";
      playerPresent = true;
    }
    else
    {
      playerPresent = false;
    }
    if(playerPresent)
    {
      msg = "Player "+to_string(current+1)+" Says ";
      msg = msg+goldMine.getMessage();
      write_message(receiver,msg);
      sem_post(my_sem_ptr);
  }
}

  // Broadcasting message

  if(key == 'b')
  {
    sem_wait(my_sem_ptr);
    int recipent = 0;
    int currentBc = giveCurrentPlayer();
    recipent = goldmap->players;
    recipent &= ~currentPlayer;
    msg = "Player "+to_string(currentBc+1)+" Says ";
    msg = msg+goldMine.getMessage();
    broadcast(recipent,msg);
    sem_post(my_sem_ptr);
  }


goldMine.drawMap();
if(key == 'l' || key == 'k' || key == 'j' || key == 'h' || key == 'Q')
{
  for (int i=0;i<5;i++)
{
    if(goldmap->pid[i]!=0)
    {
        kill(goldmap->deamonID, SIGUSR1);
        kill(goldmap->pid[i], SIGUSR1);
    }
  }
}


}
while(key != 'Q' && cFlag == FALSE);
goldmap->map[location] = 0;


     for(int i=0;i<5;i++)
     {
       if(goldmap->pid[i]!=0 && i==pidPlayer)
       {

              goldmap->pid[i] = 0;

       }
     }


  for(int i=0;i<5;i++)
  {
    if(goldmap->pid[i]!= 0 && goldmap->pid[i]!=getpid())
    {
      kill(goldmap->pid[i],SIGUSR1);
      kill(goldmap->deamonID, SIGUSR1);

    }

  }

  goldmap->players &= ~currentPlayer;


      if(goldmap->deamonID != 0)
      {
        kill(goldmap->deamonID,SIGHUP);
      }
      else
      {
        sem_close(my_sem_ptr);
        sem_unlink("/ASPgoldchase");
        shm_unlink("/ashwinsGoldMem");
      }

    mq_close(readqueue_fd);
    mq_unlink(mq_name.c_str());


   return 0;

}
