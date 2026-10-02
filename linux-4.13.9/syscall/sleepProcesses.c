#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "sleepProcesses.h"

asmlinkage long sys_listSleepingProcesses(const char __user *buf, int size) {
	struct task_struct *proces;
	char *kbuf;
	char line[128];
	int offset = 0;
	int len;
	int ret;
	int bufsz;

	if (size <= 0)
		return -1;

	kbuf = kmalloc(4096, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	for_each_process(proces) {

		//  So nos interessam processos dormindo:

		if (proces->state == TASK_INTERRUPTIBLE ||
		    proces->state == TASK_UNINTERRUPTIBLE) {

			len = snprintf(line, sizeof(line),
					"PID: %d  Name: %s  State: %s\n",
					task_pid_nr(proces),
					proces->comm,
					(proces->state == TASK_INTERRUPTIBLE) ?
						"TASK_INTERRUPTIBLE" :
						"TASK_UNINTERRUPTIBLE");

			// buffer cheio
			if (offset + len >= 4096 - 1)
				break;

			memcpy(kbuf + offset, line, len);
			offset += len;
		}
	}
	kbuf[offset] = '\0';
	bufsz = offset + 1;  // inclui o '\0' 

	if (bufsz > size) {
		kfree(kbuf);
		return -1;
	}

	ret = copy_to_user((void *)buf, (void *)kbuf, bufsz);
	kfree(kbuf);

	// Segue o mesmo padrao de retorno do processInfo.c:
	return bufsz - ret;
}
