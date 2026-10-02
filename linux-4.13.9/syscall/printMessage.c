#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include "printMessage.h"

asmlinkage long sys_printMessage(const char __user *msg) {
	char kbuf[256];
	long len;

	if (msg == NULL)
		return -EINVAL;

	len = strncpy_from_user(kbuf, msg, 256 - 1);

	// endereco invalido
	if (len < 0) {
		return len;
	}

	// string do usuario for maior que o buffer 
	kbuf[len] = '\0';

	printk(KERN_INFO "sys_printMessage: %s\n", kbuf);
	return len;
}
