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

#include "gpio_uapi.h"


#define SYSTICK_DEVICE_PATH   "/dev/rtdm/DMTimer7"


// types
struct xeno_systick_task;
struct xeno_systick_task_ops
{
  int (*nrt_init)(struct xeno_systick_task *task);
  int (*rt_init)(struct xeno_systick_task *task);
  void (*do_work)(struct xeno_systick_task *task, int event_cnt);
  int (*rt_deinit)(struct xeno_systick_task *task);
  int (*nrt_deinit)(struct xeno_systick_task *task);
};

struct xeno_systick_task
{
  const char *systick_device_path;
  RT_TASK xeno_task;
  // pipe for the communication with the normal Linux domain (mainly for logging instead of printf)
  const char *msg_pipe_name;
  RT_PIPE msg_pipe;
  // flag signaling when the task should exit
  volatile int do_work;

  struct xeno_systick_task_ops ops;
  // work specific data
  void *private;
};


int xeno_task_log(struct xeno_systick_task *task,  char *data, int data_len);
void xeno_task_set_work_data(struct xeno_systick_task *task, void *data);
void *xeno_task_get_work_data(struct xeno_systick_task *task);







// called by normal Linux process, before launching the rt task (no pipe exists yet!!!)
int nrt_init_periodic_work(struct xeno_systick_task *task)
{
	gpio_init();
	return 0;
}

// called from primary mode
int rt_init_periodic_work(struct xeno_systick_task *task)
{
	return 0;

}

// called from primary mode
void do_periodic_work(struct xeno_systick_task *task, int event_cnt)
{
  char msg[32];
  int msg_len;

  gpio_toggle();

  msg_len = sprintf(msg,"Event count: %d\n",event_cnt);

  xeno_task_log(task,msg,msg_len);
}

// called from primary mode
int rt_deinit_periodic_work(struct xeno_systick_task *task)
{
	return 0;

}

// called by normal Linux process after destroying the rt thread
int nrt_deinit_periodic_work(struct xeno_systick_task *task)
{
	gpio_deinit();
	return 0;
}

struct xeno_systick_task_ops my_ops = 
{
  .nrt_init   = nrt_init_periodic_work,
  .rt_init    = rt_init_periodic_work,
  .do_work    = do_periodic_work,
  .rt_deinit  = rt_deinit_periodic_work,
  .nrt_deinit = nrt_deinit_periodic_work
};


//****************** TASK RUNNER ******************

// WORK API
int xeno_task_log(struct xeno_systick_task *task,  char *data, int data_len)
{
  return rt_pipe_write(&task->msg_pipe,data,data_len,P_NORMAL);
}

void xeno_task_set_work_data(struct xeno_systick_task *task, void *data)
{
  task->private = data;
}
void *xeno_task_get_work_data(struct xeno_systick_task *task)
{
  return task->private;
}

// RUNNER
void xeno_periodic_task_runner(void *arg)
{
  struct xeno_systick_task *task = (struct xeno_systick_task*)arg;

  int fd;
  int ret;
  uint32_t buf;
  int pipe_minor;


  // creating message pipe to the normal linux thread for communication
  pipe_minor = rt_pipe_create(&task->msg_pipe, task->msg_pipe_name, P_MINOR_AUTO, 1024);
  if(pipe_minor <  0)
  {
    fprintf(stderr,"[RT RUNNER] Pipe creation failed.\n");
    return;
  }


  fprintf(stderr,"[RT RUNNER] Pipe created with minor id: %d\n",pipe_minor);


  // open system tick character device
  fd = __COBALT(open(task->systick_device_path,O_RDWR));
  if(fd<=0)
  {
    fprintf(stderr,"[RT RUNNER] Cannot open systick device.\n");
    return;
  }
  else
  {
    fprintf(stderr,"[RT RUNNER] Systick device opended successfully.\n");
  }
  // enable irq events
  ret = __COBALT(ioctl(fd,UDD_RTIOC_IRQDIS));
  if(ret)
    fprintf(stderr,"[RT RUNNER] Cannot disable interrupts. [%d - %s]\n",ret,strerror(errno));

  ret = __COBALT(ioctl(fd,UDD_RTIOC_IRQEN));
  if(ret)
    fprintf(stderr,"[RT RUNNER] Cannot enable interrupts. [%d - %s]\n",ret,strerror(errno));
  __COBALT(close(fd));




  // reopen systick file (it zeroes the event counter)
  fd = __COBALT(open(task->systick_device_path,O_RDWR));
  if(fd<=0)
  {
    fprintf(stderr,"[RT RUNNER] Cannot reopen systick device.\n");
    return;
  }

  fprintf(stderr,"[RT RUNNER] Starting real time service.\n");


  task->ops.rt_init(task);
  while(task->do_work)
  {
    ret = __COBALT(read(fd,&buf,4));
    if(ret != sizeof(buf))
      break;

    task->ops.do_work(task,buf); 
  }
  task->ops.rt_deinit(task);


  __COBALT(ioctl(fd,UDD_RTIOC_IRQDIS));
  __COBALT(close(fd));

  rt_pipe_delete(&task->msg_pipe);

  fprintf(stderr,"[RT RUNNER] Stopping real time service...\n");
}

// INIT / DEINIT API
int xeno_periodic_task_start(struct xeno_systick_task *task, const char *task_name, const char *pipe_name, const char *systick_path, struct xeno_systick_task_ops *ops)
{
  task->systick_device_path = systick_path;
  task->msg_pipe_name = pipe_name;
  task->do_work = 1;
  task->ops = *ops;

  task->ops.nrt_init(task);

  rt_task_create(&task->xeno_task, task_name, 0, 50, T_JOINABLE);
  return  rt_task_start(&task->xeno_task, &xeno_periodic_task_runner, (void*)task);
}

int xeno_periodic_task_term(struct xeno_systick_task *task)
{
  task->do_work = 0;
  return 0;
}

int xeno_periodic_task_join(struct xeno_systick_task *task)
{
  int ret;
  ret = rt_task_join(&task->xeno_task);
  task->ops.nrt_deinit(task);
  return ret;
}

//**************** LOGGER THREAD ******************

void* pipe_logger(void *arg)
{
  int fd_pipe;
  int retry = 0;
  int rcvCnt;
  char buf[128];
  const char *pipe_path = (char *)arg;

  fd_pipe = -1;
  retry = 0;
  while((fd_pipe <0) && (retry < 10))
  {

    fd_pipe = open(pipe_path, O_RDWR);
    if(fd_pipe < 0)
    {
      usleep(1000+retry*100000);
      retry++;
    }
  }
  if(retry == 10)
  {
    fprintf(stdout,"[LOGGER] Cannot open the log pipe.\n");
    return NULL;
  }

  fprintf(stderr,"[LOGGER] Receiving messages from the RT thread.\n");

  // read the messages from the RT thread and print them to the console
  while(1)
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

  close(fd_pipe);
  return NULL;
}

//****************** INIT THREAD ******************

struct xeno_systick_task task;

int sigterm_cnt = 0;
void signal_handler(int sig)
{
  printf("Sending termination signal...\n");
  (void)sig;

  if(++sigterm_cnt == 5)
  {
    printf("Forcing exit...\n");
    exit(-1);
  }
}


int main()
{
  pthread_t logger;

  //Lock the memory to avoid memory swapping for this program
  mlockall(MCL_CURRENT | MCL_FUTURE);

  signal(SIGINT, signal_handler);

  printf("[MAIN] Starting xenomai system task...\n");
  xeno_periodic_task_start(&task, "Xeno_task","xeno_pipe",SYSTICK_DEVICE_PATH,&my_ops);
  pthread_create(&logger,NULL,&pipe_logger,"/proc/xenomai/registry/rtipc/xddp/xeno_pipe");

  pause();

  fprintf(stderr,"[MAIN] Waking rt thread\n");
  xeno_periodic_task_term(&task);
  fprintf(stderr,"[MAIN] Waiting for the rt thread.\n");
  xeno_periodic_task_join(&task);

  // logger thread will be terminated by the op system.

  printf("Exiting...\n");
  return 0;
}