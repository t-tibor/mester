#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <alchemy/task.h>
#include <alchemy/timer.h>
#include <math.h>
#include <errno.h>

#include "rtdm/udd.h"
#include "alchemy/pipe.h"


#define SYSTICK_DEVICE_PATH   "/dev/rtdm/DMTimer7"

RT_TASK xeno_task;
#define MSG_PIPE_NAME         "xeno_pipe"
RT_PIPE msg_pipe;

volatile int do_work;



void periodic_work(int event_cnt)
{
  char msg[32];
  int msg_len;
  msg_len = sprintf(msg,"Event count: %d\n",event_cnt);

  rt_pipe_write(&msg_pipe,msg,msg_len,P_NORMAL);
}



void xeno_periodic_task(void *arg)
{
  RT_TASK *curtask;
  RT_TASK_INFO curtaskinfo;
  int fd;
  int ret;
  uint32_t buf;
  int pipe_minor;


  // creating message pipe to the normal linux thread for communication
  pipe_minor = rt_pipe_create(&msg_pipe, MSG_PIPE_NAME, P_MINOR_AUTO, 1024);
  if(pipe_minor <  0)
  {
    fprintf(stderr,"Pipe creation failed.\n");
    return;
  }


  printf("Pipe created with minor id: %d\n",pipe_minor);


  // open system tick character device
  fd = __COBALT(open(SYSTICK_DEVICE_PATH,O_RDWR));
  if(fd<=0)
  {
    fprintf(stderr,"Cannot open systick device.\n");
    return;
  }
  else
  {
    fprintf(stderr,"Systick device opended successfully.\n");
  }
  // enable irq events
  ret = __COBALT(ioctl(fd,UDD_RTIOC_IRQDIS));
  if(ret)
    fprintf(stderr,"Cannot disable interrupts. [%d - %s]\n",ret,strerror(errno));

  ret = __COBALT(ioctl(fd,UDD_RTIOC_IRQEN));
  if(ret)
    fprintf(stderr,"Cannot enable interrupts. [%d - %s]\n",ret,strerror(errno));
  __COBALT(close(fd));




  // reopen systick file (it zeroes the event counter)
  fd = __COBALT(open(SYSTICK_DEVICE_PATH,O_RDWR));
  if(fd<=0)
  {
    fprintf(stderr,"Cannot reopen systick device.\n");
    return;
  }

  fprintf(stderr,"Starting real time service.\n");

  while(do_work)
  {
    ret = __COBALT(read(fd,&buf,4));
    if(ret != sizeof(buf))
      break;

    periodic_work(buf);    
  }

  rt_pipe_write(&msg_pipe,"Exit",7,P_NORMAL);
  // wait for man thread answer
  rt_pipe_read(&msg_pipe,&buf,4,TM_INFINITE);


  __COBALT(ioctl(fd,UDD_RTIOC_IRQDIS));
  __COBALT(close(fd));

  rt_pipe_delete(&msg_pipe);

  fprintf(stderr,"Stopping real time service...\n");
}


void signal_handler(int sig)
{
  printf("Sending termination signal...\n");
  (void)sig;
  if(do_work)
  {
    do_work = 0;
  }
  else
  {
    printf("Forcing exit...\n");
    exit(-1);
  }
}


int main(int argc, char **argv)
{
  int fd_pipe;
  char buf[128];
  int rcvCnt;
  int retry;

  //Lock the memory to avoid memory swapping for this program
  mlockall(MCL_CURRENT | MCL_FUTURE);
    
  printf("Starting xenomai system task...\n");

  do_work = 1;
  signal(SIGINT, signal_handler);

  //Create the real time task
  rt_task_create(&xeno_task, "Xeno_task", 0, 50, T_JOINABLE);
  rt_task_start(&xeno_task, &xeno_periodic_task, 0);


  fd_pipe = -1;
  retry = 0;
  while((fd_pipe <0) && (retry < 10))
  {

    fd_pipe = open("/proc/xenomai/registry/rtipc/xddp/xeno_pipe", O_RDWR);
    if(fd_pipe < 0)
    {
      usleep(1000+retry*100000);
      retry++;
    }
  }
  if(retry == 10)
    goto out;

  fprintf(stderr,"Receiving messages from the RT thread.\n");

  // read the messages from the RT thread and print them to the console
  while(do_work)
  {
    rcvCnt = read(fd_pipe,buf,127);
    if(rcvCnt == -1)
    {
      fprintf(stderr,"Pipe read error: %s\n",strerror(errno));
      break;
    }
    else if(rcvCnt == 0)
    {
      break;
    }
    else
    {
      buf[rcvCnt] = 0;
      printf("%s",buf);
    }
  }
  // send ack to the rt thread
  write(fd_pipe,buf,4);
  close(fd_pipe);

out:
  do_work = 0;
  printf("Waiting for the RT thread.\n");
  rt_task_join(&xeno_task);
  printf("Exiting...\n");

  return 0;
}