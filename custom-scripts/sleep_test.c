#include <stdio.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>

#define SYSCALL_SLEEPPROCESSES	386

int main(int argc, char** argv){
	char buf[4096];
	long ret;

	printf("Invoking 'listSleepingProcesses' system call.\n");

	ret = syscall(SYSCALL_SLEEPPROCESSES, buf, sizeof(buf));

	if(ret > 0) {
		printf("%s\n", buf);
	}
	else {
		printf("System call 'listSleepingProcesses' did not execute as expected, error %ld\n", ret);
	}

	return 0;
}
