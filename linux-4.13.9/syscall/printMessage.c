#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include "printMessage.h"

/* Tamanho maximo da mensagem aceita (incluindo o terminador '\0') */
#define MSG_MAX_LEN 256

asmlinkage long sys_printMessage(const char __user *msg) {
	char kbuf[MSG_MAX_LEN];
	long len;

	if (msg == NULL)
		return -EINVAL;

	/* Copia a string do espaco de usuario para o kernel de forma segura:
	 * strncpy_from_user() trata os casos de endereco invalido (retorna
	 * -EFAULT) e de string maior que o buffer (trunca em MSG_MAX_LEN-1
	 * e nao estoura o kbuf). Em caso de sucesso, retorna o numero de
	 * bytes copiados (sem contar o '\0'). */
	len = strncpy_from_user(kbuf, msg, MSG_MAX_LEN - 1);

	if (len < 0) {
		/* Endereco de usuario invalido */
		return len;
	}

	/* Garante terminador, mesmo se a string do usuario for maior
	 * que o buffer (nesse caso strncpy_from_user nao copia o '\0') */
	kbuf[len] = '\0';

	printk(KERN_INFO "sys_printMessage: %s\n", kbuf);

	/* Numero de bytes efetivamente impressos */
	return len;
}
