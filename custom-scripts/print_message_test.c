#include <stdio.h>
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>

#define SYSCALL_PRINTMESSAGE	387

void usage(char* s){
	printf("Usage: %s <mensagem>\n", s);
	exit(0);
}

int main(int argc, char** argv){
	long ret;

	if(argc < 2){
		usage(argv[0]);
	}

	printf("Invoking 'printMessage' system call.\n");

	ret = syscall(SYSCALL_PRINTMESSAGE, argv[1]);

	if(ret >= 0) {
		printf("Mensagem enviada ao log do kernel (%ld bytes). Confira com 'dmesg'.\n", ret);
	}
	else {
		printf("System call 'printMessage' did not execute as expected, error %ld\n", ret);
	}

	return 0;
}
