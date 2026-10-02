/** @brief Verifica a ordem FIFO e o retorno da fila vazia. */
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

	// Envia tudo antes de ler. 
	for (i = 0; i < n; i++){
		printf("Escrevendo mensagem %d: [%s]\n", i + 1, mensagens[i]);
		ret = write(fd, mensagens[i], strlen(mensagens[i]));
		if (ret < 0){
			perror("Failed to write the message to the device.");
			close(fd);
			return errno;
		}
	}

	// Confere se as mensagens retornam na ordem de envio. 
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

	// Confirma que uma leitura extra encontra a fila vazia. 
	memset(receive, 0, BUFFER_LENGTH);
	ret = read(fd, receive, BUFFER_LENGTH);
	printf("Leitura extra (fila deveria estar vazia): retornou %d byte(s) %s\n",
		ret, (ret == 0) ? "(OK, fila vazia)" : "(inesperado)");

	close(fd);
	printf("Fim do teste\n");

	return 0;
}
