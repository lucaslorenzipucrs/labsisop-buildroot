/*
 * SSTF (Shortest Seek Time First) IO Scheduler
 *
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
	sector_t last_sector; 
};

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

/* Distancia entre a posicao atual da cabeca e o setor solicitado. */
static inline sector_t sstf_seek_distance(sector_t head, sector_t pos)
{
	return (pos > head) ? (pos - head) : (head - pos);
}

/* Despacha a requisicao mais proxima; empates mantem a ordem da fila. */
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

		/* Atualiza a posicao simulada da cabeca. */
		nd->last_sector = blk_rq_pos(chosen);
		direction = (rq_data_dir(chosen) == WRITE) ? 'W' : 'R';

		printk(KERN_EMERG "[SSTF] dsp %c %llu\n", direction,
			(unsigned long long)blk_rq_pos(chosen));

		return 1;
	}
	return 0;
}

/* Adiciona ao fim da fila para preservar a ordem em caso de empate. */
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

/* Registra as operacoes do escalonador no kernel. */
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

static int __init sstf_init(void)
{
	return elv_register(&elevator_sstf);
}

static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);

MODULE_AUTHOR("GRUPO_G");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SSTF IO scheduler");
