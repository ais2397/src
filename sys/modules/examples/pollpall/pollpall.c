
/*	$NetBSD: ,v 1.1 2018/04/20 00:06:45  Exp $    */

/*-
* Copyright (c) 2018 The NetBSD Foundation, Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
* ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
* PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: pollpall.c,v 1.1 2018/04/20 00:06:45  Exp $");

#include <sys/param.h>
#include <sys/module.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/poll.h>
#include <sys/select.h>
#include <sys/condvar.h>


/*
	* Create a device /dev/pollpall
	*
	* To use this device you need to do:
	*     mknod /dev/pollpall c 210 0
	*
	*Commentary:
	*This module manages device /dev/pollpall
	*/


dev_type_open(pollpall_open);
dev_type_close(pollpall_close);
dev_type_write(pollpall_write);
dev_type_read(pollpall_read);
dev_type_poll(pollpall_poll);


static struct cdevsw pollpall_cdevsw = {
	.d_open = pollpall_open,
	.d_close = pollpall_close,
	.d_read = pollpall_read,
	.d_write = pollpall_write,
	.d_ioctl = noioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = pollpall_poll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER | D_MPSAFE,
};


struct pollpall_softc {
	int		refcnt;
	char *buf;
	size_t buf_len;
	kmutex_t lock;
	struct selinfo psel;
	kcondvar_t sc_cv;
	enum states {
		IDLE=0,
		READ_WAITING = 1,
		WRITE_WAITING = 2
	} sc_state; /*device can have three states 1.idle, 2.where read is at wait 3. where write is in wait queue.*/
};


static struct pollpall_softc sc;

/*Function that checks whether the string entered is a pallindrome*/
int
check_pall(const char *str,size_t len)
{
	int n=0;
	while (n<=len/2){
		if (str[n]!=str[len-n-1]){
			return 0;
		}
		n++;
	}
	return 1;
}



int
pollpall_open(dev_t self __unused, int flag __unused, int mode __unused,struct
lwp *l __unused)
{
	if (sc.refcnt > 2) //device file can be atmost opened 2 times.
		return EBUSY;

	++sc.refcnt;

	return 0;
}



int
pollpall_close(dev_t self __unused, int flag __unused, int mode __unused,
struct lwp *l __unused)
{
	--sc.refcnt;
	return 0;
}


/*Device would write when state is IDLE or READ_WAITING.Once device would finish
	*writing when it's in IDLE state, the state would change to WRITE_WAITING.
	*If the device was in READ_WAITING state then it would go back to IDLE STATE.
	*/
int
pollpall_write(dev_t self __unused, struct uio *uio, int flags __unused)
{
	printf("---Write---\n");

	/*Ensures only write process accesses the shared data*/
	mutex_enter(&sc.lock);

	/* Unblocks the waiters */
	cv_broadcast(&sc.sc_cv);
	switch(sc.sc_state) {

	/* In IDLE state both read and write will be supported. So device can write
	 *in this state.
	 */
	case IDLE:
		sc.sc_state=WRITE_WAITING;
		/* Wakes up the threads waiting on write to be executed */
		selnotify(&sc.psel, POLLOUT | POLLWRNORM,0);
		while (sc.sc_state==WRITE_WAITING) {
			if (sc.buf) {
				int error=cv_wait_sig(&sc.sc_cv,&sc.lock); //helps in synchronising read and write, if the buffer has not been read(emptied), it would wait.
				if (error) {
					return error;
				}
			}
			sc.buf_len = uio->uio_iov->iov_len; //
			sc.buf = (char*)kmem_alloc(sc.buf_len, KM_SLEEP);
			uiomove(sc.buf, sc.buf_len, uio);
			printf("Use cat to know the result :)\n");
			break;
		}
		break;

	/* Only write can be executed in READ_WAITING state */
	case READ_WAITING:
		selnotify(&sc.psel, POLLOUT | POLLWRNORM,0);
		if (sc.buf) {
			int error=cv_wait_sig(&sc.sc_cv,&sc.lock);	//helps in synchronising read and write, if the buffer has not been read(emptied), it would wait.
			if (error) {
				return error;
			}
		}
		sc.buf_len = uio->uio_iov->iov_len;
		sc.buf = (char*)kmem_alloc(sc.buf_len, KM_SLEEP);
		uiomove(sc.buf, sc.buf_len, uio);
		printf("Use cat to know the result :)\n");
		sc.sc_state=IDLE;
		break;

	/* Only read can be executed in READ_WAITING state */
	case WRITE_WAITING:
		printf("WRITE_WAITING");
		break;
	}
	mutex_exit(&sc.lock);
	return 0;
}

/*Device would write when state is IDLE or READ_WAITING.Once device would finish
	*writing when it's in IDLE state, the state would change to WRITE_WAITING.
	*If the device was in READ_WAITING state then it would go back to IDLE STATE.
	*/
int
pollpall_read(dev_t self __unused, struct uio *uio, int flags __unused)
{
	int r;
	printf("---READING---\n");

	/*Ensures only write process accesses the shared data*/
	mutex_enter(&sc.lock);

	/* Unblocks the waiters */
	cv_broadcast(&sc.sc_cv);
	switch(sc.sc_state){

	/* In IDLE state both read and write will be supported. So device can write
 	 *in this state.
	 */
	case IDLE:
		sc.sc_state=READ_WAITING;
		selnotify(&sc.psel, POLLIN | POLLRDNORM,0);  //wakes up the threads waiting on read to be executed
		while(sc.sc_state==READ_WAITING) {
			if (!sc.buf){
				int error = cv_wait_sig(&sc.sc_cv,&sc.lock);		//helps in synchronising read and write, if the buffer has not been filled by write or write was not executed before read, it would wait.
				if (error) {
					return error;
				}
			}
			printf("The string you entered was: ");
			uiomove(sc.buf,sc.buf_len, uio);
			printf("\n");
			if (check_pall(sc.buf,sc.buf_len)) {
				printf("String was a pallindrome\n");
			}
			else {
				printf("String was not a pallindrome\n");
			}
			break;
		}
		break;

	/* Only write can be executed in READ_WAITING state */
	case READ_WAITING:
		printf("READ_WAITING");
		break;

	/* Only read can be executed in READ_WAITING state */
	case WRITE_WAITING:
		selnotify(&sc.psel, POLLIN | POLLRDNORM,0);
		if (!sc.buf) {
			int error = cv_wait_sig(&sc.sc_cv,&sc.lock);
			if (error) {
				return error;
			}
		}
		printf("The string you entered was: ");
		uiomove(sc.buf,sc.buf_len, uio);
		if (check_pall(sc.buf,sc.buf_len)) {
			printf("String was a pallindrome");
		}
		else{
			printf("String was not a pallindrome");
		}
		sc.sc_state=IDLE;
	}
	kmem_free(sc.buf, sc.buf_len);
	sc.buf=NULL;
	mutex_exit(&sc.lock);
	return 0;
}



int
pollpall_poll(dev_t dev, int events, struct lwp *l)
{
	printf("Entering poll\n");
	int revents = 0;
	/* Ensures shared data is accessed only by this thread. */
	mutex_enter(&sc.lock);
	/*When device would be in IDLE state it can READ as well as it can WRITE. */
	if (sc.sc_state==IDLE) {
		if (events && POLLOUT | POLLWRNORM) {
			revents |= POLLOUT | POLLWRNORM;
		}

		if (events &&  POLLIN | POLLRDNORM) {
			revents |= POLLIN | POLLRDNORM;
		}

	}
	/*when device is in state where read process has to wait then it can only
	*write in any other condition it would just record that request using
	*selrecord()
	*/
	else if(sc.sc_state == READ_WAITING){
		if (events && (POLLWRNORM | POLLOUT)) {
			revents |= POLLOUT;
		}
		else{
			selrecord(l, &sc.psel);
		}
	}

	/*When device is in state where write process has to wait then it can only
	 *read ; in any other condition it would just record that request using
	 *selrecord()
	 */
	else if(sc.sc_state == WRITE_WAITING) {
		if (events && (POLLIN | POLLRDNORM)) {
			revents |= POLLIN ;
		}
		else {
			selrecord(l, &sc.psel);
		}
	}
	printf("Exiting poll\n");
	mutex_exit(&sc.lock);
	return revents;
}

MODULE(MODULE_CLASS_MISC, pollpall, NULL);

static int
pollpall_modcmd(modcmd_t cmd, void *arg __unused)
{
/* The major should be verified and changed if needed to avoid
 *conflicts with other devices.
 */
	int cmajor = 210, bmajor = -1;

	switch (cmd) {
	case MODULE_CMD_INIT:
		if (devsw_attach("pollpall", NULL, &bmajor, &pollpall_cdevsw,&cmajor))
			return ENXIO;
		printf("Module has been loaded\n");
		sc.sc_state=IDLE;
		selinit(&sc.psel);
		cv_init(&sc.sc_cv, "sc_cv");
		mutex_init(&sc.lock, MUTEX_DEFAULT, IPL_NET);
		return 0;

	case MODULE_CMD_FINI:
		if (sc.refcnt > 0)
			return EBUSY;
		printf("Module has been unloaded\n");
		seldestroy(&sc.psel);
		cv_destroy(&sc.sc_cv);
		mutex_destroy(&sc.lock);
		devsw_detach(NULL, &pollpall_cdevsw);
		return 0;
	default:
		return ENOTTY;
	}
}
