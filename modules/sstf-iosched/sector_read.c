/* Gera leituras concorrentes em /dev/sdb para exercitar o SSTF. */
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
	system("echo 3 > /proc/sys/vm/drop_caches"); // Limpa o cache do disco.

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
		lseek(fd, (off_t)pos * 512, SEEK_SET);
		read(fd, buf, 100);
	}
	close(fd);

	return 0;
}
