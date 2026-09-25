/**
 * @brief Extensao da Atividade 2: driver de criptografia XTEA
 * (/dev/xtea_driver) que agora recebe a chave via PARAMETRO DE MODULO no
 * momento do carregamento, em vez de exigir a chave em cada comando
 * write(). Baseado no mecanismo de module_param descrito no LKMPG:
 * http://www.tldp.org/LDP/lkmpg/2.6/html/lkmpg.html#AEN323
 *
 * Carregamento:
 *   $ modprobe xtea_driver key0="f0e1d2c3" key1="b4a59687" \
 *              key2="78695a4b" key3="3c2d1e0f"
 *
 * Se nenhum parametro for passado, usa a chave padrao (a mesma do
 * exemplo do enunciado da Atividade 2).
 *
 * IMPORTANTE: para "modprobe xtea_driver ..." funcionar, o MODULO
 * (arquivo .ko) precisa se chamar "xtea_driver" -- e' por isso que este
 * arquivo fonte e' xtea_driver.c e o Makefile usa "obj-m := xtea_driver.o".
 * Isso e' diferente do nome do DEVICE (DEVICE_NAME, que tambem e'
 * "xtea_driver" e determina o caminho /dev/xtea_driver) -- sao dois
 * "nomes" com papeis diferentes que aqui coincidem por conveniencia.
 *
 * PROTOCOLO (mudou em relacao a Atividade 2 -- a chave nao faz mais
 * parte do comando, ja que agora e' fixa por chave carregada no modulo):
 *
 *   <cmd> <tamanho> <dados_hex>
 *
 *   cmd       : "enc" (cifra) ou "dec" (decifra)
 *   tamanho   : quantidade de bytes dos dados, decimal, multiplo de 8
 *   dados_hex : os bytes de entrada em hexadecimal (2*tamanho caracteres)
 *
 *   Exemplo:  enc 8 1234567890123456
 *
 * O resultado (hex) fica disponivel no proximo read(), igual a
 * Atividade 2.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>

#define DEVICE_NAME "xtea_driver"   ///< Aparece em /dev/xtea_driver
#define CLASS_NAME  "cripto_class"

#define XTEA_ROUNDS      32
#define XTEA_BLOCK_BYTES 8
#define MAX_DATA_BYTES   256

/* Buffer de entrada: "enc "/"dec " + numero do tamanho + espacos +
 * 2*MAX_DATA_BYTES hex + folga. Bem menor que na Atividade 2 porque a
 * chave saiu do comando. */
#define MAX_CMD_LEN (16 + 16 + 2*MAX_DATA_BYTES + 16)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author Name");
MODULE_DESCRIPTION("Driver de criptografia XTEA com chave via module_param.");
MODULE_VERSION("0.2");

// ---------------------------------------------------------------------
// Parametros de modulo: a chave XTEA (128 bits = 4 palavras de 32 bits),
// cada uma passada como string hexadecimal no carregamento do modulo.
// ---------------------------------------------------------------------

static char *key0 = "f0e1d2c3";
static char *key1 = "b4a59687";
static char *key2 = "78695a4b";
static char *key3 = "3c2d1e0f";

/* module_param(nome_da_variavel, tipo, permissao_em_sysfs).
 * "charp" = string (char *). 0444 deixa o valor visivel (somente
 * leitura) em /sys/module/xtea_driver/parameters/keyN depois que o
 * modulo estiver carregado -- passe 0 no lugar de 0444 se preferir nao
 * expor a chave ali. O NOME do parametro no modprobe e' o nome da
 * variavel C (key0, key1, key2, key3), a menos que se use
 * module_param_named() para dar um nome diferente. */
module_param(key0, charp, 0444);
MODULE_PARM_DESC(key0, "Palavra 0 (32 bits, hex) da chave XTEA");
module_param(key1, charp, 0444);
MODULE_PARM_DESC(key1, "Palavra 1 (32 bits, hex) da chave XTEA");
module_param(key2, charp, 0444);
MODULE_PARM_DESC(key2, "Palavra 2 (32 bits, hex) da chave XTEA");
module_param(key3, charp, 0444);
MODULE_PARM_DESC(key3, "Palavra 3 (32 bits, hex) da chave XTEA");

/* Chave efetiva, ja convertida de string hex para inteiro (feito uma
 * vez em cripto_init(), a partir de key0..key3 acima). */
static u32 xtea_key[4];

static int majorNumber;
static struct class  *cryptoClass  = NULL;
static struct device *cryptoDevice = NULL;
static int numberOpens = 0;

static DEFINE_MUTEX(dev_lock);
static char *result_hex = NULL;
static size_t result_len = 0;

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
// Helpers de hex <-> bytes / u32 (big-endian explicito, mesma
// convencao da Atividade 2)
// ---------------------------------------------------------------------

static const char hex_digits[] = "0123456789abcdef";

static int hex_nibble(char c){
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

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

/** @brief Converte uma string hex (ate' 8 caracteres) para u32.
 *  Usada so' para interpretar key0..key3 recebidos como parametro de
 *  modulo -- escrita na mao (em vez de kstrtou32) para nao depender de
 *  nenhum header alem dos ja incluidos.
 *  @return 0 em sucesso, -1 se a string for invalida/vazia/> 8 chars. */
static int hexstr_to_u32(const char *s, u32 *out){
	size_t i, len = strlen(s);
	u32 val = 0;

	if (len == 0 || len > 8)
		return -1;

	for (i = 0; i < len; i++){
		int nib = hex_nibble(s[i]);
		if (nib < 0)
			return -1;
		val = (val << 4) | (u32)nib;
	}
	*out = val;
	return 0;
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

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
	size_t to_copy;
	int error_count;

	mutex_lock(&dev_lock);

	if (!result_hex) {
		mutex_unlock(&dev_lock);
		printk(KERN_INFO "Cripto Driver: nenhum resultado disponivel ainda\n");
		return 0;
	}

	to_copy = min(len, result_len + 1);
	error_count = copy_to_user(buffer, result_hex, to_copy);

	mutex_unlock(&dev_lock);

	if (error_count != 0){
		printk(KERN_INFO "Cripto Driver: falhou ao enviar resultado ao usuario\n");
		return -EFAULT;
	}

	printk(KERN_INFO "Cripto Driver: enviados %zu bytes de resultado ao usuario\n", to_copy);
	return to_copy;
}

/** @brief Interpreta o comando "enc"/"dec" e executa a operacao XTEA
 *  usando a chave fixa (xtea_key) carregada no modulo. */
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
	char *kbuf;
	char cmd[8];
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

	/* Novo protocolo (sem chave no comando): cmd tamanho dados_hex */
	n = sscanf(kbuf, "%7s %u %s", cmd, &data_size, hexdata);

	if (n != 3){
		printk(KERN_ALERT "Cripto Driver: formato de comando invalido (esperado: cmd tamanho dados_hex)\n");
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

	nblocks = data_size / XTEA_BLOCK_BYTES;
	for (i = 0; i < nblocks; i++){
		u32 v[2];
		unsigned char *block_in  = rawdata + i * XTEA_BLOCK_BYTES;
		unsigned char *block_out = outdata + i * XTEA_BLOCK_BYTES;

		v[0] = bytes_to_u32_be(block_in);
		v[1] = bytes_to_u32_be(block_in + 4);

		if (is_enc)
			xtea_encipher(XTEA_ROUNDS, v, xtea_key);
		else
			xtea_decipher(XTEA_ROUNDS, v, xtea_key);

		u32_to_bytes_be(v[0], block_out);
		u32_to_bytes_be(v[1], block_out + 4);
	}

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

	/* Converte os parametros de modulo (strings hex) para a chave
	 * efetiva usada pelo XTEA. Se algum for invalido, o modulo recusa
	 * a carga (return != 0 em um initcall impede o modprobe/insmod). */
	if (hexstr_to_u32(key0, &xtea_key[0]) != 0){
		printk(KERN_ALERT "Cripto Driver: key0 invalido: '%s' (precisa ser hex, ate 8 digitos)\n", key0);
		return -EINVAL;
	}
	if (hexstr_to_u32(key1, &xtea_key[1]) != 0){
		printk(KERN_ALERT "Cripto Driver: key1 invalido: '%s'\n", key1);
		return -EINVAL;
	}
	if (hexstr_to_u32(key2, &xtea_key[2]) != 0){
		printk(KERN_ALERT "Cripto Driver: key2 invalido: '%s'\n", key2);
		return -EINVAL;
	}
	if (hexstr_to_u32(key3, &xtea_key[3]) != 0){
		printk(KERN_ALERT "Cripto Driver: key3 invalido: '%s'\n", key3);
		return -EINVAL;
	}

	printk(KERN_INFO "Cripto Driver: chave carregada: %08x %08x %08x %08x\n",
		xtea_key[0], xtea_key[1], xtea_key[2], xtea_key[3]);

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
