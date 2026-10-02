/**
 * @brief   An introductory character driver. This module maps to /dev/simple_driver and
 * comes with a helper C program that can be run in Linux user space to communicate with
 * this the LKM.
 *
 * Modified from Derek Molloy (http://www.derekmolloy.ie/)
 *
 */

#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>
#include <linux/list.h>           // struct list_head e macros para manipular listas
#include <linux/slab.h>           // kmalloc / kfree
#include <linux/mutex.h>          // protege a fila contra acessos concorrentes

#define  DEVICE_NAME "simple_driver" ///< The device will appear at /dev/simple_driver using this value
#define  CLASS_NAME  "simple_class"        ///< The device class -- this is a character device driver
#define  MAX_MSG_SIZE 256                  ///< Tamanho maximo

MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("Author Name");    ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("A generic Linux char driver com fila de mensagens (Atividade 1).");
MODULE_VERSION("0.3");            ///< A version number to inform users

static int    majorNumber;                  ///< Stores the device number -- determined automatically
static int    numberOpens = 0;              ///< Counts the number of times the device is opened
static struct class *charClass  = NULL; ///< The device-driver class struct pointer
static struct device *charDevice = NULL; ///< The device-driver device struct pointer

/** @brief Elemento da fila: guarda a mensagem, seu tamanho e o elo da lista. */
struct msg_node {
	char *data;
	size_t size;
	struct list_head list;
};

/** @brief Inicializa a fila vazia. */
static LIST_HEAD(msg_queue);

/** @brief Protege a fila contra acessos concorrentes. */
static DEFINE_MUTEX(msg_queue_lock);

// The prototype functions for the character driver -- must come before the struct definition
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);


/** @brief Devices are represented as file structure in the kernel. The file_operations structure from
 *  /linux/fs.h lists the callback functions that you wish to associated with your file operations
 *  using a C99 syntax structure. char devices usually implement open, read, write and release calls
 */
static struct file_operations fops =
{
	.open = dev_open,
	.read = dev_read,
	.write = dev_write,
	.release = dev_release,
};


/** @brief The LKM initialization function
 *  @return returns 0 if successful
 */
static int __init simple_init(void){
	printk(KERN_INFO "Simple Driver: Initializing the LKM\n");

	// Try to dynamically allocate a major number for the device -- more difficult but worth it
	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber<0){
		printk(KERN_ALERT "Simple Driver failed to register a major number\n");
		return majorNumber;
	}

	printk(KERN_INFO "Simple Driver: registered correctly with major number %d\n", majorNumber);

	// Register the device class
	charClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(charClass)){                // Check for error and clean up if there is
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Simple Driver: failed to register device class\n");
		return PTR_ERR(charClass);          // Correct way to return an error on a pointer
	}

	printk(KERN_INFO "Simple Driver: device class registered correctly\n");

	// Register the device driver
	charDevice = device_create(charClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
	if (IS_ERR(charDevice)){               // Clean up if there is an error
		class_destroy(charClass);           // Repeated code but the alternative is goto statements
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Simple Driver: failed to create the device\n");
		return PTR_ERR(charDevice);
	}

	printk(KERN_INFO "Simple Driver: device class created correctly\n"); // Made it! device was initialized

	return 0;
}


/** @brief The LKM cleanup function
 *  Remove o dispositivo e libera as mensagens que ainda estiverem na fila.
 */
static void __exit simple_exit(void){
	struct msg_node *node, *tmp;

	mutex_lock(&msg_queue_lock);
	/* Percorre a fila com seguranca enquanto remove seus elementos. */
	list_for_each_entry_safe(node, tmp, &msg_queue, list) {
		list_del(&node->list);
		kfree(node->data);
		kfree(node);
	}
	mutex_unlock(&msg_queue_lock);

	device_destroy(charClass, MKDEV(majorNumber, 0));     // remove the device
	class_unregister(charClass);                          // unregister the device class
	class_destroy(charClass);                             // remove the device class
	unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number
	printk(KERN_INFO "Simple Driver: goodbye from the LKM!\n");
}


/** @brief The device open function that is called each time the device is opened */
static int dev_open(struct inode *inodep, struct file *filep){
	numberOpens++;
	printk(KERN_INFO "Simple Driver: device has been opened %d time(s)\n", numberOpens);
	return 0;
}


/**  @brief Retira e retorna a mensagem mais antiga da fila.
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 *  @param buffer The pointer to the buffer to which this function writes the data
 *  @param len The length (capacidade) do buffer do usuario
 *  @param offset The offset if required (nao usado aqui)
 */
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){
	struct msg_node *node;
	size_t to_copy;
	int error_count;

	mutex_lock(&msg_queue_lock);

	if (list_empty(&msg_queue)) {
		mutex_unlock(&msg_queue_lock);
		printk(KERN_INFO "Simple Driver: fila de mensagens vazia\n");
		return 0;
	}

	node = list_first_entry(&msg_queue, struct msg_node, list);
	list_del(&node->list); // Remove o no da fila. 

	mutex_unlock(&msg_queue_lock);

	to_copy = min(len, node->size);

	error_count = copy_to_user(buffer, node->data, to_copy);

	if (error_count != 0){
		printk(KERN_INFO "Simple Driver: falhou ao enviar %d bytes ao usuario\n", error_count);
		kfree(node->data);
		kfree(node);
		return -EFAULT;
	}

	printk(KERN_INFO "Simple Driver: enviados %zu caracteres ao usuario\n", to_copy);

	kfree(node->data);
	kfree(node);

	return to_copy; // Numero de bytes copiados
}


	 //**  @brief Copia uma mensagem do usuario e a adiciona.
 *  @param filep A pointer to a file object
 *  @param buffer The buffer to that contains the string to write to the device
 *  @param len The length of the array of data that is being passed in the const char buffer
 *  @param offset The offset if required
 */
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
	struct msg_node *node;

	if (len == 0 || len > MAX_MSG_SIZE){
		printk(KERN_ALERT "Simple Driver: tamanho de mensagem invalido (%zu)\n", len);
		return -EINVAL;
	}

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->data = kmalloc(len, GFP_KERNEL);
	if (!node->data){
		kfree(node);
		return -ENOMEM;
	}

	// copia a mensagem do espaco do usuario para o kernel com seguranca. 
	if (copy_from_user(node->data, buffer, len)){
		kfree(node->data);
		kfree(node);
		return -EFAULT;
	}

	node->size = len;
	INIT_LIST_HEAD(&node->list);

	mutex_lock(&msg_queue_lock);
	list_add_tail(&node->list, &msg_queue); 
	mutex_unlock(&msg_queue_lock);

	printk(KERN_INFO "Simple Driver: recebidos %zu caracteres do usuario, adicionados a fila\n", len);

	return len;
}

/** @brief The device release function that is called whenever the device is closed/released by
 *  the userspace program
 */
static int dev_release(struct inode *inodep, struct file *filep){
	printk(KERN_INFO "Simple Driver: device successfully closed\n");
	return 0;
}

module_init(simple_init);
module_exit(simple_exit);
