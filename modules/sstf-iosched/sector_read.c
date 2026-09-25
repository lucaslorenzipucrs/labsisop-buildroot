/*
 * Aplicacao de geracao de I/O "raw" para testar o escalonador SSTF.
 *
 * Reaproveita o gerador do esqueleto (fork() para criar varios processos
 * concorrentes, cada um fazendo leituras em posicoes aleatorias do disco
 * "cru" /dev/sdb) - e' esse "estresse" concorrente que faz o kernel
 * acumular varias requisicoes na fila do elevador ao mesmo tempo, dando
 * ao SSTF a chance de reordena-las (com FIFO/noop nao ha nada pra
 * reordenar, ja que normalmente uma requisicao e' despachada antes da
 * proxima chegar).
 *
 * FORKS = numero de "rodadas" de fork(): como CADA processo vivo (pai e
 * todos os filhos ja criados) chama fork() de novo a cada rodada, o
 * numero total de processos cresce como 2^FORKS. Com FORKS=8 isso da'
 * 256 processos, cada um fazendo READS_PER_PROCESS leituras = ate' 2560
 * requisicoes de disco concorrentes.
 *
 * Sugestao: para os testes iniciais (so' conferir que o modulo carrega e
 * funciona) reduza para algo como FORKS=3 (8 processos) - o log do
 * dmesg fica bem menor e mais facil de ler. Suba para FORKS=8 (ou mais)
 * so' na hora de gerar o log grande para o relatorio de desempenho.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LENGTH 512
#define DISK_SZ 1073741824UL
#define FORKS 8
#define READS_PER_PROCESS 10

int main(void)
{
	int fd, i;
	unsigned int pos;
	char buf[BUFFER_LENGTH];

	printf("Starting sector read example...\n");

	printf("Cleaning disk cache...\n");
	system("echo 3 > /proc/sys/vm/drop_caches"); /* limpa buffers e caches de disco */

	printf("Configuring scheduling queues...\n");
	system("echo 2 > /sys/block/sdb/queue/nomerges");
	system("echo 4 > /sys/block/sdb/queue/max_sectors_kb");
	system("echo 0 > /sys/block/sdb/queue/read_ahead_kb");

	printf("Forking processes to put stress on disk scheduler...\n");
	for (i = 0; i < FORKS; i++)
		fork();

	srand(getpid());

	fd = open("/dev/sdb", O_RDWR);
	if (fd < 0) {
		perror("Failed to open the device...");
		return errno;
	}

	for (i = 0; i < READS_PER_PROCESS; i++) {
		pos = (rand() % (DISK_SZ >> 9));
		/* Posiciona no setor sorteado. */
		lseek(fd, (off_t)pos * 512, SEEK_SET);
		/* Executa a leitura. */
		read(fd, buf, 100);
	}
	close(fd);

	return 0;
}
