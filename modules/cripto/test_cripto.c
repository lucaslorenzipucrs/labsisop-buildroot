/**
 * @brief Aplicacao de teste do driver XTEA (/dev/xtea_driver) com chave
 * definida via module_param no carregamento do modulo.
 *
 * O protocolo enviado ao driver NAO leva mais a chave (ela ja esta fixa
 * no modulo, carregada via modprobe key0=... key1=... key2=... key3=...):
 *
 *   <cmd> <tamanho> <dados_hex>
 *
 * Mas para conferir o resultado, este teste ainda precisa saber QUAL
 * chave foi usada para carregar o modulo, e' por isso que ela pode ser
 * passada como argumento de linha de comando (tem que bater com o que
 * voce usou no modprobe!). Se voce carregou o modulo sem parametros
 * (usando a chave padrao), pode rodar o teste tambem sem argumentos.
 *
 * Uso:
 *   test_cripto                                   -> usa a chave padrao
 *   test_cripto key0 key1 key2 key3               -> usa a chave dada
 *
 * Exemplo batendo com o modprobe do enunciado:
 *   modprobe xtea_driver key0="f0e1d2c3" key1="b4a59687" \
 *            key2="78695a4b" key3="3c2d1e0f"
 *   test_cripto f0e1d2c3 b4a59687 78695a4b 3c2d1e0f
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LENGTH 1024

/* ---- XTEA de referencia (mesma do enunciado da Atividade 2) ---- */

static void encipher(uint32_t num_rounds, uint32_t v[2], const uint32_t key[4]){
	uint32_t i;
	uint32_t v0 = v[0], v1 = v[1], sum = 0, delta = 0x9E3779B9;
	for (i = 0; i < num_rounds; i++){
		v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
		sum += delta;
		v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum>>11) & 3]);
	}
	v[0] = v0; v[1] = v1;
}

static void decipher(uint32_t num_rounds, uint32_t v[2], const uint32_t key[4]){
	uint32_t i;
	uint32_t v0 = v[0], v1 = v[1], delta = 0x9E3779B9, sum = delta * num_rounds;
	for (i = 0; i < num_rounds; i++){
		v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum>>11) & 3]);
		sum -= delta;
		v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
	}
	v[0] = v0; v[1] = v1;
}

static void reference_compute(int is_enc, const uint32_t key[4],
                               const uint32_t *in_words, int nblocks,
                               char *out_hex){
	int i;
	for (i = 0; i < nblocks; i++){
		uint32_t v[2];
		v[0] = in_words[2*i];
		v[1] = in_words[2*i + 1];
		if (is_enc) encipher(32, v, key); else decipher(32, v, key);
		sprintf(out_hex + i*16, "%08x%08x", v[0], v[1]);
	}
}

static int run_case(int fd, const char *cmd_name, int is_enc,
                     const uint32_t key[4], const uint32_t *in_words, int nblocks){
	char cmd[BUFFER_LENGTH];
	char data_hex[BUFFER_LENGTH];
	char expected_hex[BUFFER_LENGTH];
	char receive[BUFFER_LENGTH];
	int ret, i, ok;

	for (i = 0; i < nblocks; i++)
		sprintf(data_hex + i*16, "%08x%08x", in_words[2*i], in_words[2*i+1]);

	snprintf(cmd, sizeof(cmd), "%s %d %s", cmd_name, nblocks * 8, data_hex);

	printf("\n> write: %s\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret < 0){ perror("write"); return -1; }

	memset(receive, 0, sizeof(receive));
	ret = read(fd, receive, sizeof(receive));
	if (ret < 0){ perror("read"); return -1; }
	printf("< read (%d bytes): %s\n", ret, receive);

	reference_compute(is_enc, key, in_words, nblocks, expected_hex);
	expected_hex[nblocks * 16] = '\0';

	ok = (strcmp(receive, expected_hex) == 0);
	printf("  esperado (calculado localmente com a chave informada): %s  %s\n",
		expected_hex, ok ? "OK" : "DIVERGIU (confira se a chave bate com a do modprobe!)");

	return ok ? 0 : -1;
}

int main(int argc, char **argv){
	int fd;
	uint32_t key[4] = {0xf0e1d2c3, 0xb4a59687, 0x78695a4b, 0x3c2d1e0f}; /* chave padrao */
	uint32_t msg1[2] = {0x12345678, 0x90123456};
	uint32_t msg2[4] = {0xdeadbeef, 0xcafebabe, 0x01234567, 0x89abcdef};

	printf("Teste do driver XTEA (/dev/xtea_driver) com chave via module_param\n");

	if (argc == 5){
		key[0] = strtoul(argv[1], NULL, 16);
		key[1] = strtoul(argv[2], NULL, 16);
		key[2] = strtoul(argv[3], NULL, 16);
		key[3] = strtoul(argv[4], NULL, 16);
		printf("Usando chave informada por argumento: %08x %08x %08x %08x\n", key[0], key[1], key[2], key[3]);
	} else if (argc == 1){
		printf("Nenhuma chave informada, usando a chave padrao (precisa bater com o modprobe): %08x %08x %08x %08x\n",
			key[0], key[1], key[2], key[3]);
	} else {
		fprintf(stderr, "Uso: %s [key0 key1 key2 key3]\n", argv[0]);
		return 1;
	}

	fd = open("/dev/xtea_driver", O_RDWR);
	if (fd < 0){
		perror("Failed to open /dev/xtea_driver");
		return errno;
	}

	printf("\n=== Caso 1: 1 bloco (8 bytes) ===\n");
	if (run_case(fd, "enc", 1, key, msg1, 1) != 0){ close(fd); return 1; }

	{
		char cmd[BUFFER_LENGTH], receive[BUFFER_LENGTH];
		uint32_t cipher_words[2];

		snprintf(cmd, sizeof(cmd), "enc 8 %08x%08x", msg1[0], msg1[1]);
		write(fd, cmd, strlen(cmd));
		memset(receive, 0, sizeof(receive));
		read(fd, receive, sizeof(receive));
		sscanf(receive, "%8x%8x", &cipher_words[0], &cipher_words[1]);

		printf("\n=== Decriptando o resultado acima, deve voltar a mensagem original ===\n");
		if (run_case(fd, "dec", 0, key, cipher_words, 1) != 0){ close(fd); return 1; }
	}

	printf("\n=== Caso 2: 2 blocos (16 bytes) ===\n");
	if (run_case(fd, "enc", 1, key, msg2, 2) != 0){ close(fd); return 1; }

	{
		char cmd[BUFFER_LENGTH], receive[BUFFER_LENGTH];
		uint32_t cipher_words[4];

		snprintf(cmd, sizeof(cmd), "enc 16 %08x%08x%08x%08x",
			msg2[0], msg2[1], msg2[2], msg2[3]);
		write(fd, cmd, strlen(cmd));
		memset(receive, 0, sizeof(receive));
		read(fd, receive, sizeof(receive));
		sscanf(receive, "%8x%8x%8x%8x",
			&cipher_words[0], &cipher_words[1], &cipher_words[2], &cipher_words[3]);

		printf("\n=== Decriptando o resultado de 2 blocos ===\n");
		if (run_case(fd, "dec", 0, key, cipher_words, 2) != 0){ close(fd); return 1; }
	}

	printf("\nTodos os testes bateram com a implementacao de referencia.\n");

	close(fd);
	return 0;
}
