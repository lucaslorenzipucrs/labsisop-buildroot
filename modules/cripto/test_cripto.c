/**
 * @brief Aplicacao de teste do driver de criptografia XTEA (/dev/xtea_driver).
 *
 * Estrategia de verificacao: a mesma implementacao XTEA de referencia do
 * enunciado (encipher/decipher, copiada aqui em espaco de usuario) e'
 * usada para calcular o resultado ESPERADO de forma independente do
 * driver, e comparamos com o que o driver realmente devolveu. Assim a
 * gente confirma que a matematica dentro do kernel bate com a referencia,
 * nao so' que "o driver respondeu alguma coisa".
 *
 * Testa dois casos:
 *   1) round-trip enc/dec de 1 bloco (8 bytes), igual ao exemplo do
 *      main() de referencia do enunciado (msg = 0x12345678 0x90123456).
 *   2) round-trip enc/dec de multiplos blocos (16 bytes), pra garantir
 *      que o driver processa varios blocos de 64 bits corretamente.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LENGTH 1024

/* ---- XTEA de referencia (copiado do enunciado, sem alteracoes) ---- */

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

/* ---- Helpers para montar/ler o protocolo hex do driver ---- */

/* Calcula localmente o resultado esperado (enc ou dec) para "nblocks"
 * blocos de 64 bits, e devolve como string hex (chamador deve ter
 * alocado "out_hex" com pelo menos 2*nblocks*8 + 1 bytes). */
static void reference_compute(int is_enc, const uint32_t key[4],
                               const uint32_t *in_words, int nblocks,
                               char *out_hex){
	int i;
	for (i = 0; i < nblocks; i++){
		uint32_t v[2];
		v[0] = in_words[2*i];
		v[1] = in_words[2*i + 1];
		if (is_enc)
			encipher(32, v, key);
		else
			decipher(32, v, key);
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

	/* monta a string hex dos dados de entrada a partir das palavras de 32 bits */
	for (i = 0; i < nblocks; i++)
		sprintf(data_hex + i*16, "%08x%08x", in_words[2*i], in_words[2*i+1]);

	snprintf(cmd, sizeof(cmd), "%s %08x %08x %08x %08x %d %s",
		cmd_name, key[0], key[1], key[2], key[3], nblocks * 8, data_hex);

	printf("\n> write: %s\n", cmd);
	ret = write(fd, cmd, strlen(cmd));
	if (ret < 0){
		perror("write");
		return -1;
	}

	memset(receive, 0, sizeof(receive));
	ret = read(fd, receive, sizeof(receive));
	if (ret < 0){
		perror("read");
		return -1;
	}
	printf("< read (%d bytes): %s\n", ret, receive);

	reference_compute(is_enc, key, in_words, nblocks, expected_hex);
	expected_hex[nblocks * 16] = '\0';

	ok = (strcmp(receive, expected_hex) == 0);
	printf("  esperado (calculado localmente): %s  %s\n", expected_hex, ok ? "OK" : "DIVERGIU");

	if (!ok)
		return -1;

	/* devolve o resultado em bytes (words) pra quem quiser encadear enc->dec */
	return 0;
}

/* converte uma string hex de volta pra um vetor de uint32_t (big-endian, %08x) */
static void hex_to_words(const char *hex, uint32_t *words, int nwords){
	int i;
	for (i = 0; i < nwords; i++)
		sscanf(hex + i*8, "%8x", &words[i]);
}

int main(void){
	int fd;
	char receive[BUFFER_LENGTH];
	char cmd[BUFFER_LENGTH];

	/* mesma chave do exemplo de referencia do enunciado */
	const uint32_t key[4] = {0xf0e1d2c3, 0xb4a59687, 0x78695a4b, 0x3c2d1e0f};

	/* mesma mensagem do main() de referencia: msg[2] = {0x12345678, 0x90123456} */
	uint32_t msg1[2] = {0x12345678, 0x90123456};

	/* mensagem maior, 2 blocos (16 bytes), pra testar múltiplos blocos */
	uint32_t msg2[4] = {0xdeadbeef, 0xcafebabe, 0x01234567, 0x89abcdef};

	printf("Teste do driver de criptografia XTEA (/dev/xtea_driver)\n");

	fd = open("/dev/xtea_driver", O_RDWR);
	if (fd < 0){
		perror("Failed to open /dev/xtea_driver");
		return errno;
	}

	printf("\n=== Caso 1: 1 bloco (8 bytes), igual ao exemplo do enunciado ===\n");
	if (run_case(fd, "enc", 1, key, msg1, 1) != 0){
		close(fd);
		return 1;
	}

	/* pega o hex retornado no read() acima de novo, pra montar o dec */
	memset(receive, 0, sizeof(receive));
	/* (o resultado do ultimo enc ja foi lido dentro de run_case; para
	 * reusa-lo aqui, repetimos o mesmo enc e lemos de novo -- mais
	 * simples do que passar o hex de volta por parametro) */
	snprintf(cmd, sizeof(cmd), "enc %08x %08x %08x %08x 8 %08x%08x",
		key[0], key[1], key[2], key[3], msg1[0], msg1[1]);
	write(fd, cmd, strlen(cmd));
	read(fd, receive, sizeof(receive));

	{
		uint32_t cipher_words[2];
		hex_to_words(receive, cipher_words, 2);
		printf("\n=== Decriptando o resultado acima, deve voltar a mensagem original ===\n");
		if (run_case(fd, "dec", 0, key, cipher_words, 1) != 0){
			close(fd);
			return 1;
		}
	}

	printf("\n=== Caso 2: 2 blocos (16 bytes) ===\n");
	if (run_case(fd, "enc", 1, key, msg2, 2) != 0){
		close(fd);
		return 1;
	}

	snprintf(cmd, sizeof(cmd), "enc %08x %08x %08x %08x 16 %08x%08x%08x%08x",
		key[0], key[1], key[2], key[3], msg2[0], msg2[1], msg2[2], msg2[3]);
	write(fd, cmd, strlen(cmd));
	memset(receive, 0, sizeof(receive));
	read(fd, receive, sizeof(receive));
	{
		uint32_t cipher_words[4];
		hex_to_words(receive, cipher_words, 4);
		printf("\n=== Decriptando o resultado de 2 blocos ===\n");
		if (run_case(fd, "dec", 0, key, cipher_words, 2) != 0){
			close(fd);
			return 1;
		}
	}

	printf("\nTodos os testes bateram com a implementacao de referencia.\n");

	close(fd);
	return 0;
}
