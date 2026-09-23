/**
 * @brief  Aplicacao de teste extra para a Atividade 1: escreve varias
 * mensagens seguidas no /dev/simple_driver e depois le de volta a mesma
 * quantidade, mostrando que elas retornam na mesma ordem em que foram
 * escritas (comportamento de fila / FIFO), e que ler alem do que foi
 * escrito devolve 0 bytes (fila vazia).
 *
 * O test_simple_driver.c original continua funcionando normalmente (ele
 * so' testa uma mensagem por vez, que e' o caso mais simples da fila).
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LENGTH 256

int main(void){
	int fd, ret;
	char receive[BUFFER_LENGTH];
	const char *mensagens[] = {
		"primeira mensagem",
		"segunda mensagem",
		"terceira mensagem"
	};
	int n = sizeof(mensagens) / sizeof(mensagens[0]);
	int i;

	printf("Teste da fila (FIFO) do simple_driver\n");

	fd = open("/dev/simple_driver", O_RDWR);
	if (fd < 0){
		perror("Failed to open the device...");
		return errno;
	}

	/* Escreve todas as mensagens antes de ler qualquer uma,
	 * pra provar que elas ficam empilhadas na fila do driver */
	for (i = 0; i < n; i++){
		printf("Escrevendo mensagem %d: [%s]\n", i + 1, mensagens[i]);
		ret = write(fd, mensagens[i], strlen(mensagens[i]));
		if (ret < 0){
			perror("Failed to write the message to the device.");
			close(fd);
			return errno;
		}
	}

	/* Agora le de volta -- deve sair na mesma ordem que foi escrita */
	for (i = 0; i < n; i++){
		memset(receive, 0, BUFFER_LENGTH);
		ret = read(fd, receive, BUFFER_LENGTH);
		if (ret < 0){
			perror("Failed to read the message from the device.");
			close(fd);
			return errno;
		}
		printf("Leitura %d (%d bytes): [%.*s]  esperado: [%s]  %s\n",
			i + 1, ret, ret, receive, mensagens[i],
			(ret == (int)strlen(mensagens[i]) && strncmp(receive, mensagens[i], ret) == 0)
				? "OK" : "DIVERGIU");
	}

	/* Uma leitura a mais deve vir vazia (fila esgotada) */
	memset(receive, 0, BUFFER_LENGTH);
	ret = read(fd, receive, BUFFER_LENGTH);
	printf("Leitura extra (fila deveria estar vazia): retornou %d byte(s) %s\n",
		ret, (ret == 0) ? "(OK, fila vazia)" : "(inesperado)");

	close(fd);
	printf("Fim do teste\n");

	return 0;
}
