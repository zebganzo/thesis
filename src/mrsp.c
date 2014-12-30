/*
 * litmus/sched_pfp.c
 *
 * Implementation of partitioned fixed-priority scheduling.
 * Based on PSN-EDF.
 */

#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <litmus/litmus.h>
#include <litmus/wait.h>
#include <litmus/jobs.h>
#include <litmus/preempt.h>
#include <litmus/fp_common.h>
#include <litmus/sched_plugin.h>
#include <litmus/sched_trace.h>
#include <litmus/trace.h>
#include <litmus/budget.h>

#include <linux/uaccess.h>
#include <litmus/fdso.h>

int init_finished;
int _flag;

/*typedef struct queue_s {
  struct list_head next;
  struct task_struct* task;
} queue_t;*/

struct mrsp_semaphore {

	struct litmus_lock litmus_lock;

	/* lock for mutual access to the struct */
	spinlock_t lock;

	/* tasks queue for resource access */
	struct list_head task_queue;

	/* current resource holder */
	struct task_struct *owner;

	/* priority ceiling for each cpu*/
	int prio_ceiling[NR_CPUS];
};

struct mrsp_state {
	int cpu_ceiling;
};

typedef struct {
	rt_domain_t 			domain;
	struct fp_prio_queue	ready_queue;
	int          			cpu;
	struct task_struct* 	scheduled; /* current task scheduled */

	struct mrsp_semaphore* 	sem;		

	struct mrsp_state*		mrsp_ceiling;
/*
 * scheduling lock slock
 * protects the domain and serializes scheduling decisions
 */
#define slock domain.ready_lock

} pfp_domain_t;

#define MIGRATION (-511)

//#define ASAP

//#define PREEMPTED_MIGRATION
//#define QUEUED_MIGRATION
//#define RUNNING_MIGRATION

DEFINE_PER_CPU(pfp_domain_t, pfp_domains);

pfp_domain_t* pfp_doms[NR_CPUS];

#define local_pfp		(&__get_cpu_var(pfp_domains))
#define remote_dom(cpu)		(&per_cpu(pfp_domains, cpu).domain)
#define remote_pfp(cpu)	(&per_cpu(pfp_domains, cpu))
#define task_dom(task)		remote_dom(get_partition(task))
#define task_pfp(task)		remote_pfp(get_partition(task))

queue_t * find_queue_entry(struct mrsp_semaphore *sem, int cpu) 
{
  struct list_head * next;
  pfp_domain_t *remote_domain;

  list_for_each(next, &(sem->task_queue)) {
    queue_t * elem;

    elem = list_entry(next, queue_t, next);

    if (get_home(elem->task) != cpu) {

    	remote_domain = task_pfp(elem->task);

    	if(remote_domain->scheduled == NULL)
    		return(elem);

    	if((remote_domain->sem->prio_ceiling[get_home(elem->task)] - 2) < get_priority(remote_domain->scheduled))
    		return(elem);
    }  
  }

  return(NULL);    
}

/* we assume the lock is being held */
static void preempt(pfp_domain_t *pfp)
{
	preempt_if_preemptable(pfp->scheduled, pfp->cpu);
}


static void fp_dequeue(pfp_domain_t* pfp, struct task_struct* t)
{
	BUG_ON(pfp->scheduled == t && is_queued(t));
	if (is_queued(t))
		fp_prio_remove(&pfp->ready_queue, t, priority_index(t));
}

static void pfp_preempt_check(pfp_domain_t *pfp)
{
	if (fp_higher_prio(fp_prio_peek(&pfp->ready_queue), pfp->scheduled))
		preempt(pfp);
}

static void pfp_domain_init(pfp_domain_t* pfp,
			       int cpu)
{
	fp_domain_init(&pfp->domain, NULL, pfp_release_jobs);
	pfp->cpu      		= cpu;
	pfp->scheduled		= NULL;
	fp_prio_queue_init(&pfp->ready_queue);
}

static void requeue(struct task_struct* t, pfp_domain_t *pfp)
{
	BUG_ON(!is_running(t));

	tsk_rt(t)->completed = 0;
	if (is_released(t, litmus_clock()))
		fp_prio_add(&pfp->ready_queue, t, priority_index(t));
	else
		add_release(&pfp->domain, t); /* it has got to wait */
}

static void job_completion(struct task_struct* t, int forced)
{
	sched_trace_task_completion(t,forced);
	TRACE_TASK(t, "job_completion().\n");

	tsk_rt(t)->completed = 0;
	prepare_for_next_period(t);
	if (is_released(t, litmus_clock()))
		sched_trace_task_release(t);
}

static void pfp_tick(struct task_struct *t)
{
	pfp_domain_t *pfp = local_pfp;

	/* Check for inconsistency. We don't need the lock for this since
	 * ->scheduled is only changed in schedule, which obviously is not
	 *  executing in parallel on this CPU
	 */
	BUG_ON(is_realtime(t) && t != pfp->scheduled);

	if (is_realtime(t) && budget_enforced(t) && budget_exhausted(t)) {
		if (!is_np(t)) {
			litmus_reschedule_local();
			TRACE("pfp_scheduler_tick: %d is preemptable => FORCE_RESCHED\n", t->pid);
		} else if (is_user_np(t)) {
			TRACE("pfp_scheduler_tick: %d is non-preemptable, preemption delayed.\n", t->pid);
			request_exit_np(t);
		}
	}
}

/* need_to_preempt - check whether the task t needs to be preempted
 */
int keep_run(struct fp_prio_queue *q, struct task_struct *t)
{
	struct task_struct *pending = fp_prio_peek(q);

	if (!pending)
		return 1;

	if (get_priority(t) < get_priority(pending))
		return 1;
	
	return 0;
}

static void mrsp_migrate_to_from_resource(int target_cpu, struct task_struct* lock_holder)
{
	struct task_struct* t = lock_holder;
	pfp_domain_t *from;


	if (get_partition(t) == target_cpu) {
		// Se sono nella mia cpu di origine controllo se ci sono job a priorita' superiore in attesa
		if (!keep_run(&task_pfp(t)->ready_queue, t))
			preempt(task_pfp(t));
		return;

	} else {
		// Migro nella mia CPU di origine sfruttando il normale meccanismo di migrazie fornito da LITMUS
		from = task_pfp(t);
		tsk_rt(t)->task_params.cpu = target_cpu;
		preempt(from);
	}
}

static void cpu_again_available_for_migration(int from_cpu, int target_cpu, struct task_struct* owner)
{
	pfp_domain_t *from = remote_pfp(from_cpu);
	pfp_domain_t *to = remote_pfp(target_cpu);

	bool fail = false;

	unsigned long	flags1;
	unsigned long	flags2;

	preempt_disable();

	raw_spin_lock_irqsave(&from->slock, flags1);
	
	// still queued here?
	if(is_queued(owner)) {
		tsk_rt(owner)->task_params.cpu = target_cpu;
		fp_dequeue(from, owner);
	} else {
		fail = true;
	}
	
	raw_spin_unlock_irqrestore(&from->slock, flags1);

	if(!fail) {
		raw_spin_lock_irqsave(&to->slock, flags2);
			owner->rt_param.task_params.priority = (to->sem->prio_ceiling[target_cpu] - 2);
			requeue(owner, to);
			preempt(to);

		raw_spin_unlock_irqrestore(&to->slock, flags2);
	}

	preempt_enable();
}

static void mrsp_dequeue_and_migrate(int from_cpu, int target_cpu, struct task_struct* owner)
{
	pfp_domain_t *from = remote_pfp(from_cpu);
	pfp_domain_t *to = remote_pfp(target_cpu);

	bool fail = false;

	local_irq_disable();

		raw_spin_lock(&from->slock);
			// Nel frattempo l'owner potrebbe essere diventato running, quindi ricontrollo
			if(is_queued(owner)) {
				tsk_rt(owner)->task_params.cpu = target_cpu;
				fp_dequeue(from, owner);
			} else {
				fail = true;
			}
		raw_spin_unlock(&from->slock);

		if(!fail) {
			raw_spin_lock(&to->slock);

			// -2 per prerilasciare il job che sta effettuando attesa attiva, il quale e' a -1
			owner->rt_param.task_params.priority = (to->sem->prio_ceiling[target_cpu] - 2);
			// lo aggiungo alla coda
			requeue(owner, to);

			raw_spin_unlock(&to->slock);
		}

	local_irq_enable();

	if(!fail) {
		preempt_enable_no_resched();
		/* deschedule to be migrated */

		schedule();
		preempt_disable();
	}
}

static void mrsp_wake_up_next_lock_holder(int from_cpu, int target_cpu, struct task_struct* owner)
{
	pfp_domain_t *from = remote_pfp(from_cpu);
	pfp_domain_t *to = remote_pfp(target_cpu);

	bool fail = false;

		local_irq_disable();

			raw_spin_lock(&from->slock);
				
				// still queued here?
				if(is_queued(owner) && tsk_rt(owner)->task_params.cpu == tsk_rt(owner)->task_params.home) {
					tsk_rt(owner)->task_params.cpu = target_cpu;
					fp_dequeue(from, owner);
				} else {
					fail = true;
				}

			raw_spin_unlock(&from->slock);

			if(!fail) {
				raw_spin_lock(&to->slock);
				
					owner->rt_param.task_params.priority = (to->sem->prio_ceiling[target_cpu] - 2);
					requeue(owner, to);
					preempt(to);

				raw_spin_unlock(&to->slock);
			}

		local_irq_enable();
}

static void mrsp_init_state(struct mrsp_state* s)
{
	s->cpu_ceiling = LITMUS_LOWEST_PRIORITY;
}

static DEFINE_PER_CPU(struct mrsp_state, mrsp_state);

static int cpu_queued(void) {
	if(__get_cpu_var(mrsp_state).cpu_ceiling < LITMUS_LOWEST_PRIORITY - 10)
		return 1;
	return 0;
}

int pfp_mrsp_lock(struct litmus_lock* l)
{
	struct task_struct* t = current;
	struct task_struct* owner = NULL;

	struct mrsp_semaphore *sem = mrsp_from_lock(l);
	queue_t * next;

	preempt_disable();

		spin_lock(&sem->lock);
		
			// Innalzo il ceiling locale
			__get_cpu_var(mrsp_state).cpu_ceiling = (sem->prio_ceiling[get_partition(t)]);
			// Aggiungo la richiesta alla coda
			queue_add_fifo(sem, t);

			// Riferimento al task in testa alla coda
			next = list_entry(sem->task_queue.next,queue_t,next);

			// Risorsa libera?
			if(sem->owner == NULL && next->task == t) {
				// Entro in possesso della risorsa e innalzo la mia priorita' (-2 per coerenza in caso di migrazioni, tutto qui)
				sem->owner = t;
				t->rt_param.task_params.priority = (sem->prio_ceiling[get_partition(t)] - 2);
			} else {

				// Risorsa occupata, innalzo la priorita' al ceiling (-1 cosi' da non essere prerilasciato)
				t->rt_param.task_params.priority = (sem->prio_ceiling[get_partition(t)] - 1);

				// Il prossimo lock holder potrebbe non aver ancora ottenuto la risorsa
				if(sem->owner != NULL)
					if(is_running(sem->owner) && is_queued(sem->owner))
						// Lock holder prerilasciato, gli cedo l'esecuzione della risorsa
						owner = sem->owner;

			}

		spin_unlock(&sem->lock);

		if(owner) {

			// Tolgo dalla coda in cui si trova e lo accodo in quella corrente (facendo attenzione che il suo stato
			// non sia cambiato nel frattempo) e chiamo la schedule()
			mrsp_dequeue_and_migrate(get_partition(owner), get_partition(t), owner);
		}
	
	preempt_enable();

	// Attesa attiva
	if(sem->owner != t)
		do {
			spin_lock(&sem->lock);
			
			next = list_entry(sem->task_queue.next,queue_t,next);

			if(sem->owner == NULL && next->task == t) {
				sem->owner = t;
				t->rt_param.task_params.priority = (sem->prio_ceiling[get_partition(t)] - 2);
			}
			spin_unlock(&sem->lock);

		} while(sem->owner != t);
	
	return 0;
}

int pfp_mrsp_unlock(struct litmus_lock* l)
{
	struct task_struct *t = current;
	struct mrsp_semaphore *sem = mrsp_from_lock(l); 

	queue_t *node;
	queue_t *next_lock_holder;
	int err = 0;

	struct task_struct* next_owner = NULL;
	int from_cpu;
	int target_cpu;

	preempt_disable();

		spin_lock(&sem->lock);
			
			sem->owner = NULL;
			
			// Ripristino il ceiling nella cpu di orgine, cosi' da limitare il tempo di blocco
			(*(pfp_doms[get_home(t)]->mrsp_ceiling)).cpu_ceiling = LITMUS_LOWEST_PRIORITY;
			// Ripristino la priorita' del job che ha rilasciato la risorsa
			t->rt_param.task_params.priority = t->rt_param.task_params.priority_for_restore;

			// Rimuovo la richiesta dalla coda
			queue_pop(sem);

			// Lista delle richieste vuota?
			if(!list_empty(&(sem->task_queue))) {

				// Il prossimo lock holder
				next_lock_holder = list_entry(sem->task_queue.next,queue_t,next);
				
				// E' accodato nella sua cpu?
				if(is_queued(next_lock_holder->task)) {

					// E' il job che sta rilasciando la risorsa a bloccarlo a causa di una migrazione?
					if(get_partition(next_lock_holder->task) != get_partition(t)) {
						
						// C'e' un processore disponibile?
						node = find_queue_entry(sem,200000);

						// Set-up per la migrazione
						if(node != NULL) {
							next_owner = next_lock_holder->task;
							from_cpu = get_partition(next_owner);
							target_cpu = get_partition(node->task);
						}
					}
				}
			}

		spin_unlock(&sem->lock);

		// Faccio migrare il prossimo lock holder
		if(next_owner) {
			mrsp_wake_up_next_lock_holder(from_cpu, target_cpu, next_owner);
		}

		// Il job, se necessario, migra alla propria CPU di origine
		if(get_partition(t) != t->rt_param.task_params.home) {
			mrsp_migrate_to_from_resource(t->rt_param.task_params.home, t);
		}

	preempt_enable();

	return err;
}

static void mrsp_update_prio_ceiling(struct mrsp_semaphore* sem,
				    int effective_prio, int cpu)
{
	unsigned long flags;

	spin_lock_irqsave(&sem->lock, flags);
	sem->prio_ceiling[cpu] = min(sem->prio_ceiling[cpu], effective_prio);
	spin_unlock_irqrestore(&sem->lock, flags);
}

int pfp_mrsp_open(struct litmus_lock* l, void* __user config)
{
	struct task_struct *t = current;
	struct mrsp_semaphore *sem = mrsp_from_lock(l);

	int cpu, eprio;

	if (!is_realtime(t))
		return -EPERM;

	cpu = get_partition(t);

	TRACE_TASK(t, "[START] pfp_mrsp_open at %llu with priority %d\n", litmus_clock(), sem->prio_ceiling[cpu]);

	if (!config)
		cpu = get_partition(t);
	else if (get_user(cpu, (int*) config))
		return -EFAULT;

	eprio = tsk_rt(t)->task_params.priority_for_restore;

	mrsp_update_prio_ceiling(sem, eprio, cpu);

	TRACE_TASK(t, "[END] pfp_mrsp_open at %llu with priority %d\n", litmus_clock(), sem->prio_ceiling[cpu]);

	return 0;
}

int pfp_mrsp_close(struct litmus_lock* l)
{
	struct task_struct *t = current;
	struct mrsp_semaphore *sem = mrsp_from_lock(l);
	unsigned long flags;

	int owner;

	spin_lock_irqsave(&sem->lock, flags);
	owner = sem->owner == t;
	spin_unlock_irqrestore(&sem->lock, flags);

	if (owner)
		pfp_mrsp_unlock(l);

	return 0;
}

void pfp_mrsp_free(struct litmus_lock* lock)
{
	kfree(mrsp_from_lock(lock));
}

static struct litmus_lock_ops pfp_mrsp_lock_ops = {
	.close  = pfp_mrsp_close,
	.lock   = pfp_mrsp_lock,
	.open	= pfp_mrsp_open,
	.unlock = pfp_mrsp_unlock,
	.deallocate = pfp_mrsp_free,
};

static void mrsp_init_semaphore(struct mrsp_semaphore* sem)
{
	int cpu;

	sem->owner = NULL;

	INIT_LIST_HEAD(&(sem->task_queue));
	
	spin_lock_init(&sem->lock);

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		sem->prio_ceiling[cpu] = (LITMUS_MAX_PRIORITY - 2);
}

static struct litmus_lock* pfp_new_mrsp(void)
{
	struct mrsp_semaphore* sem;
	int cpu;

	sem = kmalloc(sizeof(*sem), GFP_KERNEL);
	if (!sem)
		return NULL;

	sem->litmus_lock.ops = &pfp_mrsp_lock_ops;
	mrsp_init_semaphore(sem);

	for_each_online_cpu(cpu) {
		pfp_doms[cpu]->sem = sem;
	}

	return &sem->litmus_lock;
}


/* **** lock constructor **** */


static struct task_struct* pfp_schedule(struct task_struct * prev)
{
	pfp_domain_t* 	pfp = local_pfp;
	struct task_struct*	next;

	int out_of_time, sleep, preempt, np, exists, blocks, resched, migrate;
	
	// by zeb
	int lock_holder, placeholder;

	raw_spin_lock(&pfp->slock);
	
	/* sanity checking
	* differently from gedf, when a task exits (dead)
	* pfp->schedule may be null and prev _is_ realtime
	*/

	BUG_ON(pfp->scheduled && !is_realtime(prev));
	BUG_ON(pfp->scheduled && pfp->scheduled != prev);

	/* (0) Determine state */
	exists      = (pfp->scheduled != NULL) && (is_realtime(prev));
	blocks      = exists && !is_running(pfp->scheduled);
	out_of_time = exists && budget_enforced(pfp->scheduled) && budget_exhausted(pfp->scheduled);
	np 	    	= exists && is_np(pfp->scheduled);
	sleep	    = exists && is_completed(pfp->scheduled);
	migrate     = exists && get_partition(pfp->scheduled) != pfp->cpu;
	// fp_preemption_needed ::: !is_realtime(t) || fp_higher_prio(pending, t)
	preempt     = !blocks && (migrate || fp_preemption_needed(&pfp->ready_queue, prev)) && (is_realtime(prev));

	lock_holder = 0;
	placeholder = 0;

	/* If we need to preempt do so.
	* The following checks set resched to 1 in case of special
	* circumstances.
	*/
	resched = preempt;

	

	/* If a task blocks we have no choice but to reschedule.
	*/
	if (blocks)
		resched = 1;

	/* Request a sys_exit_np() call if we would like to preempt but cannot.
	* Multiple calls to request_exit_np() don't hurt.
	*/
	if (np && (out_of_time || preempt || sleep))
		request_exit_np(pfp->scheduled);

	/* Any task that is preemptable and either exhausts its execution
	* budget or wants to sleep completes. We may have to reschedule after
	* this.
	*/
	if (!np && (out_of_time || sleep) && !blocks && !migrate) {
		job_completion(pfp->scheduled, !sleep);
		resched = 1;
	}

	/*  ----------------------------------------------
	    ------- MODIFIED PFP TO SUPPORT MRSP ---------
		---------------------------------------------- */

	// Non ho bisogno di lock, se prev e' il lock holder non ci sono interleaving secondo cui non lo sara' durante l'esecuzione della schedule
	if(exists) {
		if(init_finished == 1) {
			if(prev == pfp->sem->owner) {
				lock_holder = 1;
			}
		}
	}
	/* The final scheduling decision. Do we need to switch for some reason?
	* Switch if we are in RT mode and have no task or if we need to
	* resched.
	*/

	next = NULL;
	if ((!np || blocks) && (resched || !exists)) {
		/* When preempting a task that does not block, then
		* re-insert it into either the ready queue or the
		* release queue (if it completed). requeue() picks
		* the appropriate queue.
		*/

		if(prev && preempt && lock_holder) {
			tsk_rt(prev)->task_params.cpu = MIGRATION;
			TRACE_TASK(prev, "[PFP:%llu]: [cpu %d] preempted and lock owner! I will migrate!\n", litmus_clock(), smp_processor_id());
		} else {
			if (pfp->scheduled && !blocks  && !migrate)
				requeue(pfp->scheduled, pfp);
		}

		if(init_finished == 1) {
				struct task_struct *t = fp_prio_peek(&pfp->ready_queue);

				if(t) {
					if(get_priority(t) < __get_cpu_var(mrsp_state).cpu_ceiling) {
						TRACE_TASK(t,"[PLACEHOLDER] Ok, il nuovo task puo' ESEGUIRE (t : %d - ceiling %d) \n",
							get_priority(t), __get_cpu_var(mrsp_state).cpu_ceiling);
						placeholder = 0;
					} else {
						TRACE_TASK(t,"[PLACEHOLDER] Il nuovo task deve ATTENDERE (t : %d - ceiling %d) \n",
							get_priority(t), __get_cpu_var(mrsp_state).cpu_ceiling);
						placeholder = 1;
					}
				}
		}
		
		if(placeholder == 0) {

			next = fp_prio_take(&pfp->ready_queue);
			if (next == prev) {
					struct task_struct *t = fp_prio_peek(&pfp->ready_queue);

					TRACE_TASK(next, "next==prev sleep=%d oot=%d np=%d preempt=%d migrate=%d boost=%d empty=%d prio-idx=%u prio=%u\n", sleep, out_of_time, np, preempt, migrate, is_priority_boosted(next), t == NULL, priority_index(next), get_priority(next));
					if (t)
						TRACE_TASK(t, "waiter boost=%d prio-idx=%u prio=%u\n", is_priority_boosted(t), priority_index(t), get_priority(t));
			}
		}

		/* If preempt is set, we should not see the same task again. */
		BUG_ON(preempt && next == prev);
		/* Similarly, if preempt is set, then next may not be NULL,
		* unless it's a migration. */


		/* Commentato a causa del PLACEHOLDER*/
		//BUG_ON(preempt && !migrate && next == NULL);
		if(preempt && !migrate && next == NULL) {
			TRACE_TASK(prev, "BUG_ON \n\n\n");
		}

	} else
		/* Only override Linux scheduler if we have a real-time task
		* scheduled that needs to continue.
		*/
		if (exists)
			next = prev;

	if (next) {
		TRACE_TASK(next, "scheduled at %llu\n", litmus_clock());
	} else {
		TRACE("becoming idle at %llu\n", litmus_clock());
	}

	pfp->scheduled = next;

	// Da chiamare dopo che ho deciso che job eseguire
	sched_state_task_picked();

	raw_spin_unlock(&pfp->slock);
	
	TRACE("End schedule \n");

	return next;
}

/* prev is no longer scheduled --- see if it needs to migrate */
static void pfp_finish_switch(struct task_struct *prev)
{
	pfp_domain_t *to;
	queue_t *target_node;
	unsigned long flags;

	if (is_realtime(prev) && is_running(prev) && get_partition(prev) == MIGRATION) {
		/* Caso di migrazionie dato dal prerilascio del job che possedeva il lock: 
		* get_partition(prev) == MIGRATION */

		to = local_pfp;

    	tsk_rt(prev)->task_params.cpu = smp_processor_id();

		spin_lock_irqsave(&to->sem->lock, flags);
		target_node = find_queue_entry(to->sem, smp_processor_id());
		spin_unlock_irqrestore(&to->sem->lock, flags);
	
		if(target_node != NULL) {
			tsk_rt(prev)->task_params.cpu = (get_home(target_node->task));
			prev->rt_param.task_params.priority = (to->sem->prio_ceiling[tsk_rt(prev)->task_params.cpu] - 2);
		}

		to = task_pfp(prev);

		raw_spin_lock(&to->slock);
			requeue(prev, to);
			if (fp_preemption_needed(&to->ready_queue, to->scheduled))
				preempt(to);
		raw_spin_unlock(&to->slock);

	} else if (is_realtime(prev) && is_running(prev) && get_partition(prev) != smp_processor_id()) {

		/* Caso di LITMUS per far migrare i suoi job */

		to = task_pfp(prev);

		raw_spin_lock(&to->slock);
		requeue(prev, to);
		if (fp_preemption_needed(&to->ready_queue, to->scheduled))
			preempt(to);
		raw_spin_unlock(&to->slock);

	} else {
	
		//Caso in cui il job lock holder ha dovuto migrare in un altra cpu, nella quale e' stato a sua volta prerilasciato e si trova accodato. Il job che ne aveva
		//causato la migrazione ha completato il proprio ciclo, la cpu e' tornata disponibile. Quindi o non c'e' nessun job schedulato (e' la cpu home del lock
		//holder?) o e' stato ri-selezionato per eseguire un job che sta effettuando busy wait.
		
		struct mrsp_semaphore* 	sem = local_pfp->sem;
		struct task_struct* owner = NULL;
		int from_cpu;
		int target_cpu;

		spin_lock(&sem->lock);

		if(sem->owner != NULL) {
			if(is_queued(sem->owner)) {
				if(cpu_queued()) {


					// o la cpu di origine del lock holder e' tornata disponibile
					if(local_pfp->scheduled == NULL ||
						// O un job che sta attendendo la risorsa e' tornato running
						(get_priority(local_pfp->scheduled) == (__get_cpu_var(mrsp_state).cpu_ceiling) - 1)) {

						owner = sem->owner;
						from_cpu = get_partition(owner);
						target_cpu = local_pfp->cpu;
					}
				}
			} 
		}

		spin_unlock(&sem->lock);

		// la cpu e' tornata disponibile e il lock holder e' accodato da qualche parte, lo richiamo!
		if(owner != NULL) {
			preempt_disable();
			cpu_again_available_for_migration(from_cpu, target_cpu, owner);
			preempt_enable();
		}
	}
}

void pfp_release_at(struct task_struct *t, lt_t start)
{
	// Ogni task ha un puntatore ad un nodo da porter inserire nella coda, evito allocazione di memoria a run-time!!!
	tsk_rt(t)->task_params.next = (queue_t *) kmalloc(sizeof(queue_t), GFP_KERNEL);
  	tsk_rt(t)->task_params.next->task = t;

  	// Perche' qui?!
  	pfp_doms[local_pfp->cpu]->mrsp_ceiling = (&(__get_cpu_var(mrsp_state)));
}


/*	Plugin object	*/
static struct sched_plugin pfp_plugin __cacheline_aligned_in_smp = {
	.plugin_name		= "P-FP",
	.tick				= pfp_tick,
	.task_new			= pfp_task_new,
	.complete_job		= pfp_complete_job,
	.task_exit			= pfp_task_exit,
	.schedule			= pfp_schedule,
	.task_wake_up		= pfp_task_wake_up,
	.task_block			= pfp_task_block,
	.release_at 		= pfp_release_at,
	.admit_task			= pfp_admit_task,
	.activate_plugin	= pfp_activate_plugin,
	.deactivate_plugin  = pfp_deactivate_plugin,
	.allocate_lock		= pfp_allocate_lock,
	.finish_switch		= pfp_finish_switch,
};

// instanzio il dominio in ogni processore e registro il plugin
static int __init init_pfp(void)
{
	for (int i = 0; i < num_online_cpus(); i++) {
		pfp_domain_init(remote_pfp(i), i);
	}
	return register_sched_plugin(&pfp_plugin);
}

module_init(init_pfp);
