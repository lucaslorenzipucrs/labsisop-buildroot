#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "sleepProcesses.h"

/* Tamanho do buffer interno do kernel usado para montar a lista.
 * Se houver muitos processos dormindo, a lista e' truncada de forma
 * segura (nao estoura o buffer). */
#define MAX_BUF 4096

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

	kbuf = kmalloc(MAX_BUF, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	/* Percorre todos os processos do sistema */
	for_each_process(proces) {

		/* So' nos interessam processos "dormindo":
		 *  - TASK_INTERRUPTIBLE   -> processo de usuario em sleep
		 *  - TASK_UNINTERRUPTIBLE -> processo em sleep dentro do kernel
		 * (TASK_RUNNING fica de fora, pois esta' pronto/executando) */
		if (proces->state == TASK_INTERRUPTIBLE ||
		    proces->state == TASK_UNINTERRUPTIBLE) {

			len = snprintf(line, sizeof(line),
					"PID: %d  Name: %s  State: %s\n",
					task_pid_nr(proces),
					proces->comm,
					(proces->state == TASK_INTERRUPTIBLE) ?
						"TASK_INTERRUPTIBLE" :
						"TASK_UNINTERRUPTIBLE");

			/* Para de adicionar linhas se o buffer interno encher,
			 * em vez de estourar o kbuf */
			if (offset + len >= MAX_BUF - 1)
				break;

			memcpy(kbuf + offset, line, len);
			offset += len;
		}
	}
	kbuf[offset] = '\0';
	bufsz = offset + 1; /* inclui o '\0' */

	/* Buffer do usuario e' pequeno demais */
	if (bufsz > size) {
		kfree(kbuf);
		return -1;
	}

	ret = copy_to_user((void *)buf, (void *)kbuf, bufsz);
	kfree(kbuf);

	/* Segue o mesmo padrao de retorno do processInfo.c:
	 * numero de bytes efetivamente copiados */
	return bufsz - ret;
}
