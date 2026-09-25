/**
 * @brief Atividade 2: driver de criptografia XTEA, acessivel em /dev/xtea_driver.
 *
 * Estrutura geral (registro de device, classe, file_operations) baseada no
 * mesmo padrao do Simple Driver (tutorial de Derek Molloy). O driver e' um
 * modulo NOVO (cripto.c / device xtea_driver), separado do simple_driver
 * da Atividade 1, para nao ter conflito entre os dois.
 *
 * PROTOCOLO (comando enviado via write(), texto ASCII em uma linha so):
 *
 *   <cmd> <key0> <key1> <key2> <key3> <tamanho> <dados_hex>
 *
 *   cmd       : "enc" (cifra) ou "dec" (decifra)
 *   key0..3   : 4 palavras de 32 bits da chave XTEA (128 bits), em hex,
 *               exatamente como no exemplo do enunciado
 *               (ex: f0e1d2c3 b4a59687 78695a4b 3c2d1e0f)
 *   tamanho   : quantidade de bytes dos dados, em decimal. Precisa ser
 *               multiplo de 8 (XTEA opera em blocos de 64 bits) e no
 *               maximo MAX_DATA_BYTES.
 *   dados_hex : os bytes de entrada (texto claro para "enc", texto
 *               cifrado para "dec"), em hexadecimal, exatamente
 *               2*tamanho caracteres.
 *
 *   Exemplo (igual ao do enunciado):
 *   enc f0e1d2c3 b4a59687 78695a4b 3c2d1e0f 16 aabbccddeeff00112233445566778899aabbccddeeff00
 *
 * O resultado da ultima operacao (enc ou dec) fica guardado no driver como
 * uma string hexadecimal, e e' devolvido para a aplicacao no proximo
 * read() -- assim como a entrada, tambem em hexadecimal, pra facilitar
 * comparar/copiar entre comandos.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>

#define DEVICE_NAME "xtea_driver"   ///< Aparece em /dev/xtea_driver
#define CLASS_NAME  "cripto_class"  ///< Nome de classe proprio (nao usa o do simple_driver)

#define XTEA_ROUNDS      32   ///< Numero de rounds recomendado (2 sub-rounds cada)
#define XTEA_BLOCK_BYTES 8    ///< XTEA opera em blocos de 64 bits = 8 bytes
#define MAX_DATA_BYTES   256  ///< Tamanho maximo de dados aceito por comando

/* Buffer de entrada: "enc "/"dec " + 4 chaves de 8 hex + 1 espaco cada +
 * numero do tamanho + espacos + 2*MAX_DATA_BYTES hex + folga */
#define MAX_CMD_LEN (16 + 4*9 + 16 + 2*MAX_DATA_BYTES + 16)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author Name");
MODULE_DESCRIPTION("Driver de criptografia XTEA (Atividade 2).");
MODULE_VERSION("0.1");

static int majorNumber;
static struct class  *cryptoClass  = NULL;
static struct device *cryptoDevice = NULL;
static int numberOpens = 0;

/** @brief Protege o "resultado da ultima operacao" contra acesso concorrente. */
static DEFINE_MUTEX(dev_lock);

/** @brief Resultado (em hexadecimal, terminado em '\0') da ultima operacao
 *  enc/dec, devolvido no proximo read(). NULL se nao ha nada ainda. */
static char *result_hex = NULL;
static size_t result_len = 0; /* tamanho da string, sem contar o '\0' */

// ---------------------------------------------------------------------
// XTEA (algoritmo de referencia do enunciado, adaptado para tipos do kernel)
// ---------------------------------------------------------------------

static void xtea_encipher(u32 num_rounds, u32 v[2], const u32 key[4]){
	u32 i;
	u32 v0 = v[0], v1 = v[1], sum = 0, delta = 0x9E3779B9;

	for (i = 0; i < num_rounds; i++){
		v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
		sum += delta;
		v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum>>11) & 3]);
	}
	v[0] = v0; v[1] = v1;
}

static void xtea_decipher(u32 num_rounds, u32 v[2], const u32 key[4]){
	u32 i;
	u32 v0 = v[0], v1 = v[1], delta = 0x9E3779B9, sum = delta * num_rounds;

	for (i = 0; i < num_rounds; i++){
		v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum>>11) & 3]);
		sum -= delta;
		v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
	}
	v[0] = v0; v[1] = v1;
}

// ---------------------------------------------------------------------
// Helpers de hex <-> bytes (big-endian: primeiro par de hex = byte mais
// significativo, igual a como um %08x normalmente e' lido/escrito)
// ---------------------------------------------------------------------

static const char hex_digits[] = "0123456789abcdef";

/** @return valor do nibble (0-15), ou -1 se 'c' nao for hex valido */
static int hex_nibble(char c){
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/** @brief Converte uma string hex (comprimento par) em bytes.
 *  @return 0 em sucesso, -1 se algum caractere nao for hex valido. */
static int hex_to_bytes(const char *hex, size_t hexlen, unsigned char *out){
	size_t i;
	for (i = 0; i < hexlen / 2; i++){
		int hi = hex_nibble(hex[2*i]);
		int lo = hex_nibble(hex[2*i + 1]);
		if (hi < 0 || lo < 0)
			return -1;
		out[i] = (unsigned char)((hi << 4) | lo);
	}
	return 0;
}

static void bytes_to_hex(const unsigned char *bytes, size_t n, char *out){
	size_t i;
	for (i = 0; i < n; i++){
		out[2*i]     = hex_digits[(bytes[i] >> 4) & 0xf];
		out[2*i + 1] = hex_digits[bytes[i] & 0xf];
	}
	out[2*n] = '\0';
}

static u32 bytes_to_u32_be(const unsigned char *b){
	return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | (u32)b[3];
}

static void u32_to_bytes_be(u32 v, unsigned char *b){
	b[0] = (v >> 24) & 0xff;
	b[1] = (v >> 16) & 0xff;
	b[2] = (v >> 8) & 0xff;
	b[3] = v & 0xff;
}

// ---------------------------------------------------------------------
// file_operations
// ---------------------------------------------------------------------

static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops =
{
	.open = dev_open,
	.read = dev_read,
	.write = dev_write,
	.release = dev_release,
};

static int dev_open(struct inode *inodep, struct file *filep){
	numberOpens++;
	printk(KERN_INFO "Cripto Driver: device has been opened %d time(s)\n", numberOpens);
	return 0;
}

static int dev_release(struct inode *inodep, struct file *filep){
	printk(KERN_INFO "Cripto Driver: device successfully closed\n");
	return 0;
}

/** @brief Devolve o resultado (hex) da ultima operacao enc/dec. */
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
	size_t to_copy;
	int error_count;

	mutex_lock(&dev_lock);

	if (!result_hex) {
		mutex_unlock(&dev_lock);
		printk(KERN_INFO "Cripto Driver: nenhum resultado disponivel ainda\n");
		return 0;
	}

	to_copy = min(len, result_len + 1); /* +1 para incluir o '\0' se couber */
	error_count = copy_to_user(buffer, result_hex, to_copy);

	mutex_unlock(&dev_lock);

	if (error_count != 0){
		printk(KERN_INFO "Cripto Driver: falhou ao enviar resultado ao usuario\n");
		return -EFAULT;
	}

	printk(KERN_INFO "Cripto Driver: enviados %zu bytes de resultado ao usuario\n", to_copy);
	return to_copy;
}

/** @brief Interpreta o comando "enc"/"dec" e executa a operacao XTEA. */
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
	char *kbuf;
	char cmd[8];
	unsigned int key[4];
	unsigned int data_size;
	char *hexdata;
	unsigned char *rawdata;
	unsigned char *outdata;
	char *newresult;
	int n, is_enc;
	size_t nblocks, i;

	if (len == 0 || len >= MAX_CMD_LEN){
		printk(KERN_ALERT "Cripto Driver: comando com tamanho invalido (%zu)\n", len);
		return -EINVAL;
	}

	kbuf = kmalloc(MAX_CMD_LEN, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buffer, len)){
		kfree(kbuf);
		return -EFAULT;
	}
	kbuf[len] = '\0';

	hexdata = kmalloc(MAX_CMD_LEN, GFP_KERNEL);
	if (!hexdata){
		kfree(kbuf);
		return -ENOMEM;
	}

	/* "%7s" garante que o token de comando nao estoure cmd[8], mesmo que
	 * o usuario mande algo maior que "enc"/"dec" (nesse caso o strcmp
	 * abaixo vai simplesmente rejeitar como comando invalido). */
	n = sscanf(kbuf, "%7s %x %x %x %x %u %s",
		cmd, &key[0], &key[1], &key[2], &key[3], &data_size, hexdata);

	if (n != 7){
		printk(KERN_ALERT "Cripto Driver: formato de comando invalido (esperado: cmd key0 key1 key2 key3 tamanho dados_hex)\n");
		kfree(kbuf);
		kfree(hexdata);
		return -EINVAL;
	}

	if (strcmp(cmd, "enc") == 0){
		is_enc = 1;
	} else if (strcmp(cmd, "dec") == 0){
		is_enc = 0;
	} else {
		printk(KERN_ALERT "Cripto Driver: comando desconhecido '%s' (use 'enc' ou 'dec')\n", cmd);
		kfree(kbuf);
		kfree(hexdata);
		return -EINVAL;
	}

	if (data_size == 0 || data_size > MAX_DATA_BYTES || (data_size % XTEA_BLOCK_BYTES) != 0){
		printk(KERN_ALERT "Cripto Driver: tamanho de dados invalido (%u) -- precisa ser multiplo de %d, entre 1 e %d\n",
			data_size, XTEA_BLOCK_BYTES, MAX_DATA_BYTES);
		kfree(kbuf);
		kfree(hexdata);
		return -EINVAL;
	}

	if (strlen(hexdata) != data_size * 2){
		printk(KERN_ALERT "Cripto Driver: dados_hex tem %zu caracteres, esperado %u para tamanho=%u\n",
			strlen(hexdata), data_size * 2, data_size);
		kfree(kbuf);
		kfree(hexdata);
		return -EINVAL;
	}

	rawdata = kmalloc(data_size, GFP_KERNEL);
	outdata = kmalloc(data_size, GFP_KERNEL);
	if (!rawdata || !outdata){
		kfree(kbuf); kfree(hexdata); kfree(rawdata); kfree(outdata);
		return -ENOMEM;
	}

	if (hex_to_bytes(hexdata, data_size * 2, rawdata) != 0){
		printk(KERN_ALERT "Cripto Driver: dados_hex contem caractere invalido\n");
		kfree(kbuf); kfree(hexdata); kfree(rawdata); kfree(outdata);
		return -EINVAL;
	}

	/* Processa bloco a bloco (8 bytes = 64 bits por bloco) */
	nblocks = data_size / XTEA_BLOCK_BYTES;
	for (i = 0; i < nblocks; i++){
		u32 v[2];
		unsigned char *block_in  = rawdata + i * XTEA_BLOCK_BYTES;
		unsigned char *block_out = outdata + i * XTEA_BLOCK_BYTES;

		v[0] = bytes_to_u32_be(block_in);
		v[1] = bytes_to_u32_be(block_in + 4);

		if (is_enc)
			xtea_encipher(XTEA_ROUNDS, v, key);
		else
			xtea_decipher(XTEA_ROUNDS, v, key);

		u32_to_bytes_be(v[0], block_out);
		u32_to_bytes_be(v[1], block_out + 4);
	}

	/* Monta o novo resultado (hex) e substitui o anterior */
	newresult = kmalloc(data_size * 2 + 1, GFP_KERNEL);
	if (!newresult){
		kfree(kbuf); kfree(hexdata); kfree(rawdata); kfree(outdata);
		return -ENOMEM;
	}
	bytes_to_hex(outdata, data_size, newresult);

	mutex_lock(&dev_lock);
	kfree(result_hex);
	result_hex = newresult;
	result_len = data_size * 2;
	mutex_unlock(&dev_lock);

	printk(KERN_INFO "Cripto Driver: %s de %u bytes concluida, resultado: %s\n",
		is_enc ? "encriptacao" : "decriptacao", data_size, newresult);

	kfree(kbuf);
	kfree(hexdata);
	kfree(rawdata);
	kfree(outdata);

	return len;
}

// ---------------------------------------------------------------------
// init / exit
// ---------------------------------------------------------------------

static int __init cripto_init(void){
	printk(KERN_INFO "Cripto Driver: Initializing the LKM\n");

	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber < 0){
		printk(KERN_ALERT "Cripto Driver failed to register a major number\n");
		return majorNumber;
	}
	printk(KERN_INFO "Cripto Driver: registered correctly with major number %d\n", majorNumber);

	cryptoClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(cryptoClass)){
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Cripto Driver: failed to register device class\n");
		return PTR_ERR(cryptoClass);
	}
	printk(KERN_INFO "Cripto Driver: device class registered correctly\n");

	cryptoDevice = device_create(cryptoClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
	if (IS_ERR(cryptoDevice)){
		class_destroy(cryptoClass);
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Cripto Driver: failed to create the device\n");
		return PTR_ERR(cryptoDevice);
	}
	printk(KERN_INFO "Cripto Driver: device class created correctly\n");

	return 0;
}

static void __exit cripto_exit(void){
	mutex_lock(&dev_lock);
	kfree(result_hex);
	result_hex = NULL;
	mutex_unlock(&dev_lock);

	device_destroy(cryptoClass, MKDEV(majorNumber, 0));
	class_unregister(cryptoClass);
	class_destroy(cryptoClass);
	unregister_chrdev(majorNumber, DEVICE_NAME);
	printk(KERN_INFO "Cripto Driver: goodbye from the LKM!\n");
}

module_init(cripto_init);
module_exit(cripto_exit);
