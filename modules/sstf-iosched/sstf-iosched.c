/*
 * SSTF (Shortest Seek Time First) IO Scheduler
 *
 * Para o kernel 4.13.9.
 *
 * Baseado no esqueleto fornecido pelo professor (por sua vez uma copia
 * do noop-iosched.c do proprio kernel, com printk's de instrumentacao
 * ja adicionados). A UNICA diferenca de comportamento em relacao ao
 * noop esta na funcao de dispatch (sstf_dispatch): em vez de sempre
 * remover o PRIMEIRO elemento da fila (ordem estrita de chegada = FIFO,
 * que e exatamente o que o noop faz), a politica SSTF varre a fila
 * inteira e escolhe, a cada dispatch, a requisicao cujo setor inicial
 * esta mais proximo da posicao atual (simulada) da cabeca de
 * leitura/escrita do disco - minimizando a distancia total percorrida
 * pelo braco do disco ao longo do atendimento de todas as requisicoes.
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

/* SSTF data structure. */
struct sstf_data {
	struct list_head queue;
	sector_t last_sector; /* posicao atual (simulada) da cabeca do disco,
				* atualizada a cada requisicao despachada. */
};

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

/* Distancia (em setores) entre a posicao atual da cabeca e o inicio de
 * uma requisicao - o "custo de seek" que o SSTF tenta minimizar a cada
 * escolha. sector_t e' um tipo sem sinal, entao a subtracao e' feita de
 * forma a nunca estourar (underflow). */
static inline sector_t sstf_seek_distance(sector_t head, sector_t pos)
{
	return (pos > head) ? (pos - head) : (head - pos);
}

/* Esta funcao despacha o proximo bloco a ser lido/escrito, seguindo a
 * politica SSTF: dentre TODAS as requisicoes atualmente na fila, escolhe
 * a que exige o menor deslocamento da cabeca (|setor_requisicao -
 * setor_atual|). Em caso de empate, prevalece a que chegou primeiro na
 * fila (list_for_each_entry percorre do inicio ao fim, e so troca de
 * "chosen" quando encontra uma distancia ESTRITAMENTE menor).
 *
 * Antes de retornar, imprime o sector que foi atendido (msg "[SSTF] dsp").
 */
static int sstf_dispatch(struct request_queue *q, int force){
	struct sstf_data *nd = q->elevator->elevator_data;
	struct request *rq, *chosen = NULL;
	sector_t chosen_dist = 0, dist;
	char direction;

	list_for_each_entry(rq, &nd->queue, queuelist) {
		dist = sstf_seek_distance(nd->last_sector, blk_rq_pos(rq));
		if (!chosen || dist < chosen_dist) {
			chosen = rq;
			chosen_dist = dist;
		}
	}

	if (chosen) {
		list_del_init(&chosen->queuelist);
		elv_dispatch_sort(q, chosen);

		/* A cabeca do disco "se move" para o setor atendido. */
		nd->last_sector = blk_rq_pos(chosen);
		direction = (rq_data_dir(chosen) == WRITE) ? 'W' : 'R';

		printk(KERN_EMERG "[SSTF] dsp %c %llu\n", direction,
			(unsigned long long)blk_rq_pos(chosen));

		return 1;
	}
	return 0;
}

/* Aqui adicionamos uma requisicao na fila do driver. A ordem de insercao
 * em si nao decide a ordem de atendimento (quem decide isso e' o
 * dispatch, varrendo a fila inteira a cada chamada) - continuamos
 * inserindo no fim da lista (FIFO na insercao) apenas para preservar a
 * ordem de chegada como criterio de desempate em sstf_dispatch.
 *
 * Antes de retornar, imprime o sector que foi adicionado (msg "[SSTF] add").
 */
static void sstf_add_request(struct request_queue *q, struct request *rq){
	struct sstf_data *nd = q->elevator->elevator_data;
	char direction = (rq_data_dir(rq) == WRITE) ? 'W' : 'R';

	list_add_tail(&rq->queuelist, &nd->queue);

	printk(KERN_EMERG "[SSTF] add %c %llu\n", direction,
		(unsigned long long)blk_rq_pos(rq));
}

static int sstf_init_queue(struct request_queue *q, struct elevator_type *e){
	struct sstf_data *nd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = nd;

	INIT_LIST_HEAD(&nd->queue);
	nd->last_sector = 0;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	return 0;
}

static void sstf_exit_queue(struct elevator_queue *e)
{
	struct sstf_data *nd = e->elevator_data;

	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
}

/* Infraestrutura dos drivers de IO Scheduling. */
static struct elevator_type elevator_sstf = {
	.ops.sq = {
		.elevator_merge_req_fn		= sstf_merged_requests,
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_init_fn		= sstf_init_queue,
		.elevator_exit_fn		= sstf_exit_queue,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

/* Inicializacao do driver. */
static int __init sstf_init(void)
{
	return elv_register(&elevator_sstf);
}

/* Finalizacao do driver. */
static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);

MODULE_AUTHOR("GRUPO_G");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheduler");
