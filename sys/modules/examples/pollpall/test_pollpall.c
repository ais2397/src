#include <stdio.h>
#include<poll.h>
#include<string.h>
#include<fcntl.h>
#include<unistd.h>
#include<errno.h>

#define BUFFLEN 100
#define TIMEOUT 10000
int main(void){
  struct pollfd fds[2];
  int ret,rfd,wfd;
  int len;
  const char *wbuf = "abcdefghijklmnopqrstuvwxyz";; /* should wbuf be a constant string or can it be scanned by scanf each time*/
  char rbuf[BUFFLEN];
  int total_bytes=strlen(wbuf);
  int i=1;

  printf("Test poll.\n");



  wfd = open("/dev/pollpall", O_WRONLY | O_NONBLOCK,0);
  fds[0].fd = wfd;
  fds[0].events |= POLLOUT;
  if (wfd==-1){
    printf("error for wfd %d\n",errno);
  }

  rfd = open("/dev/pollpall", O_RDONLY,0);
  fds[1].fd = rfd;
  fds[1].events |= POLLIN;
  if (rfd==-1){
    printf("error for rfd %d\n",errno);
  }

  while(1){
    printf("poll_executing %d times\n",i);
    ret = poll(fds, 2, TIMEOUT);

    if (ret==0){
      printf("%d seconds elapsed \n.TIMEOUT.\n", TIMEOUT/1000);
      return 0;
    }


    if (fds[0].revents && POLLOUT){
      printf("Device is writeable\n");
      ret = write(wfd,wbuf,strlen(wbuf));
      printf( Wrote %d byte\n",ret);
    }

    if (fds[1].revents && POLLIN ){
      printf("Device is readable\n");
      memset(rbuf,0,BUFFLEN);
      ret= read(rfd, rbuf, total_bytes);
      printf("Read %d bytes\n",ret);
    }
    i++;
  }
close(rfd);
close(wfd);
return 0;
}
