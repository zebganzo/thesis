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

void print_queue(struct mrsp_semaphore *sem) 
{
  struct list_head * next;

  TRACE("Init Print\n");

  list_for_each(next, &(sem->task_queue)) {
    queue_t * elem;
    elem = list_entry(next, queue_t, next);
    TRACE_TASK(elem->task," <- task queue and the home is %d\n", get_home(elem->task));
  }

  TRACE("Finish Print\n");
}

void print_queue_star(struct mrsp_semaphore *sem) 
{
  struct list_head * next;

  TRACE("[*]Init Print\n");

  list_for_each(next, &(sem->task_queue)) {
    queue_t * elem;
    elem = list_entry(next, queue_t, next);
    TRACE_TASK(elem->task,"[*] <- task queue and the home is %d\n", get_home(elem->task));
  }

  TRACE("[*]Finish Print\n");
}

//
// Add an element to the end of the list, so it will be the last one popped
//

queue_t * queue_add_fifo(struct mrsp_semaphore *sem, struct task_struct* task)
{
  /*queue_t * next;
  next = (queue_t *) kmalloc(sizeof(queue_t), GFP_KERNEL);
  next->task = task;*/

  list_add_tail(&tsk_rt(task)->task_params.next->next, &(sem->task_queue) );

  return(tsk_rt(task)->task_params.next);
}

//
// Add an element to the head of the list, so it will be the next one popped
//

queue_t * queue_add_lifo(struct mrsp_semaphore *sem, struct task_struct* task)
{
  queue_t * next;

  next = (queue_t *) kmalloc(sizeof(queue_t), GFP_KERNEL);
  next->task = task;
  
  //
  //  Add to the head for LIFO (stack) queueing
  //

  list_add(&next->next, &(sem->task_queue) );

  return(next);
}

//
// Pop an element off the head of the queue
//

queue_t * queue_pop(struct mrsp_semaphore *sem)
{
  //
  // Check if the queue is empty
  //
  if (!list_empty(&(sem->task_queue))) {
    queue_t * next;
    
    // 
    // Get the first entry on the list. This is the one that task_queue.next points to.
    // 
    next = list_entry(sem->task_queue.next,queue_t,next);
    list_del(&next->next);
    return(next);
  } else {
    return(NULL);
  }
}

//
// Pop an element off the tail of the queue
//

queue_t * queue_pop_tail(struct mrsp_semaphore *sem)
{
  //
  // Check if the queue is empty
  //
  if (!list_empty(&(sem->task_queue))) {
    queue_t * next;
    
    // 
    // Get the last entry on the list. This is the one that task_queue.prev points to.
    // 
    next = list_entry(sem->task_queue.prev,queue_t,next);
    list_del(&next->next);
    return(next);
  } else {
    return(NULL);
  }
}

queue_t * find_queue_entry(struct mrsp_semaphore *sem, int cpu) 
{
  struct list_head * next;
  pfp_domain_t *remote_domain;

  list_for_each(next, &(sem->task_queue)) {
    queue_t * elem;

    elem = list_entry(next, queue_t, next);

    if (get_home(elem->task) != cpu) {

    	remote_domain = task_pfp(elem->task);

    	if(remote_domain->scheduled == NULL) {
    		return(elem);
    	}

    	if((remote_domain->sem->prio_ceiling[get_home(elem->task)] - 2) < get_priority(remote_domain->scheduled)) {
    		return(elem);
    	}

    }
  }
  return(NULL);    
}

//
// Delete all entries from the queue that match the data value
//
void del_queue_entry(struct mrsp_semaphore *sem, struct task_struct* task) 
{
  struct list_head * next;
  struct list_head * temp;

  //
  // Iterate over the list. We need to use the safe iterator because we may want to 
  // remove things.
  //

  list_for_each_safe(next, temp, &(sem->task_queue)) {
    queue_t * elem;

    //
    // Get a pointer to the element on the list
    //
    elem = list_entry(next, queue_t, next);
    
    //
    // Delete the element if the data field matches
    //

    if (elem->task == task) {
      list_del(&elem->next);
    }
  }
    
}

/*8888888888888888888888888888888888888888888888888888888888*/

/* we assume the lock is being held */
static void preempt(pfp_domain_t *pfp)
{
	preempt_if_preemptable(pfp->scheduled, pfp->cpu);
}

static unsigned int priority_index(struct task_struct* t)
{
#ifdef CONFIG_LITMUS_LOCKING
	if (unlikely(t->rt_param.inh_task))
		/* use effective priority */
		t = t->rt_param.inh_task;

	if (is_priority_boosted(t)) {
		/* zero is reserved for priority-boosted tasks */
		return 0;
	} else
#endif
		return get_priority(t);
}

static void fp_dequeue(pfp_domain_t* pfp, struct task_struct* t)
{
	BUG_ON(pfp->scheduled == t && is_queued(t));
	if (is_queued(t))
		fp_prio_remove(&pfp->ready_queue, t, priority_index(t));
}

static void pfp_release_jobs(rt_domain_t* rt, struct bheap* tasks)
{
	pfp_domain_t *pfp = container_of(rt, pfp_domain_t, domain);
	unsigned long flags;
	struct task_struct* t;
	struct bheap_node* hn;

	raw_spin_lock_irqsave(&pfp->slock, flags);

	while (!bheap_empty(tasks)) {
		hn = bheap_take(fp_ready_order, tasks);
		t = bheap2task(hn);
		TRACE_TASK(t, "released (part:%d prio:%d)\n",
			   get_partition(t), get_priority(t));
		fp_prio_add(&pfp->ready_queue, t, priority_index(t));
	}

	/* do we need to preempt? */
	if (fp_higher_prio(fp_prio_peek(&pfp->ready_queue), pfp->scheduled)) {
		TRACE_CUR("preempted by new release\n");
		preempt(pfp);
	}

	raw_spin_unlock_irqrestore(&pfp->slock, flags);
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

/*	Prepare a task for running in RT mode
 */
static void pfp_task_new(struct task_struct * t, int on_rq, int is_scheduled)
{
	pfp_domain_t* 	pfp = task_pfp(t);
	unsigned long		flags;

	/* setup job parameters */
	release_at(t, litmus_clock());

	raw_spin_lock_irqsave(&pfp->slock, flags);
	if (is_scheduled) {
		/* there shouldn't be anything else running at the time */
		BUG_ON(pfp->scheduled);
		pfp->scheduled = t;
	} else if (is_running(t)) {
		requeue(t, pfp);
		/* maybe we have to reschedule */
		pfp_preempt_check(pfp);
	}
	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

static void pfp_task_wake_up(struct task_struct *task)
{
	unsigned long		flags;
	pfp_domain_t*		pfp = task_pfp(task);
	lt_t			now;

	TRACE_TASK(task, "wake_up at %llu\n", litmus_clock());
	raw_spin_lock_irqsave(&pfp->slock, flags);

	/* Should only be queued when processing a fake-wake up due to a
	 * migration-related state change. */
	if (unlikely(is_queued(task))) {
		TRACE_TASK(task, "WARNING: waking task still queued. Is this right?\n");
		goto out_unlock;
	}

	now = litmus_clock();
	if (is_sporadic(task) && is_tardy(task, now)
	/* We need to take suspensions because of semaphores into
	 * account! If a job resumes after being suspended due to acquiring
	 * a semaphore, it should never be treated as a new job release.
	 */
	    && !is_priority_boosted(task)) {
		/* new sporadic release */
		release_at(task, now);
		sched_trace_task_release(task);
	}

	/* Only add to ready queue if it is not the currently-scheduled
	 * task. This could be the case if a task was woken up concurrently
	 * on a remote CPU before the executing CPU got around to actually
	 * de-scheduling the task, i.e., wake_up() raced with schedule()
	 * and won. Also, don't requeue if it is still queued, which can
	 * happen under the DPCP due wake-ups racing with migrations.
	 */
	if (pfp->scheduled != task) {
		requeue(task, pfp);
		pfp_preempt_check(pfp);
	}

#ifdef CONFIG_LITMUS_LOCKING
out_unlock:
#endif
	raw_spin_unlock_irqrestore(&pfp->slock, flags);
	TRACE_TASK(task, "wake up done\n");
}

static void pfp_task_block(struct task_struct *t)
{
	/* only running tasks can block, thus t is in no queue */
	TRACE_TASK(t, "[pfp_task_block] block at %llu, state=%d\n", litmus_clock(), t->state);

	BUG_ON(!is_realtime(t));

	/* If this task blocked normally, it shouldn't be queued. The exception is
	 * if this is a simulated block()/wakeup() pair from the pull-migration code path.
	 * This should only happen if the DPCP is being used.
	 */
#ifdef CONFIG_LITMUS_LOCKING
	if (unlikely(is_queued(t)))
		TRACE_TASK(t, "WARNING: blocking task still queued. Is this right?\n");
#else
	BUG_ON(is_queued(t));
#endif
}

static void pfp_task_exit(struct task_struct * t)
{
	unsigned long flags;
	pfp_domain_t* 	pfp = task_pfp(t);
	rt_domain_t*		dom;

	raw_spin_lock_irqsave(&pfp->slock, flags);
	if (is_queued(t)) {
		BUG(); /* This currently doesn't work. */
		/* dequeue */
		dom  = task_dom(t);
		remove(dom, t);
	}
	if (pfp->scheduled == t) {
		pfp->scheduled = NULL;
		preempt(pfp);
	}

	TRACE_TASK(t, "RIP, now reschedule\n");

	raw_spin_unlock_irqrestore(&pfp->slock, flags);
}

/* -----------   MRSP SUPPORT ---------- */

/* Promemoria:
 -  Prima task_new poi _open
 - *1
*/

/* need_to_preempt - check whether the task t needs to be preempted
 */
int keep_run(struct fp_prio_queue *q, struct task_struct *t)
{
	struct task_struct *pending;

	pending = fp_prio_peek(q);

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
		// If I am in my own cpu, I will check If preemption is need
		if (!keep_run(&task_pfp(t)->ready_queue, t))
			preempt(task_pfp(t));
		return;

	} else {
		from = task_pfp(t);

		tsk_rt(t)->task_params.cpu = target_cpu;

		preempt(from);
	}

#ifdef RUNNING_MIGRATION
	if(_flag == 0) {

		TS_MRSP2_START;
		TS_MRSP2_END;
		
		_flag++;
		t->rt_param.task_params.migrating = 1;
	}
#endif
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

			owner->rt_param.task_params.priority = (to->sem->prio_ceiling[target_cpu] - 2);
			requeue(owner, to);

			raw_spin_unlock(&to->slock);
		}

	local_irq_enable();

	if(!fail) {
		preempt_enable_no_resched();
		/* deschedule to be migrated */

#ifdef QUEUED_MIGRATION
			if(_flag == 0) {

				TS_MRSP2_START;
				TS_MRSP2_END;
				
				_flag++;
				owner->rt_param.task_params.migrating = 1;
			}
#endif

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

#ifdef QUEUED_MIGRATION
					if(_flag == 0) {

						TS_MRSP2_START;
						TS_MRSP2_END;
						
						_flag++;
						owner->rt_param.task_params.migrating = 1;
					}
#endif

				raw_spin_unlock(&to->slock);
			}

		local_irq_enable();
}

static void perform_ASAP(struct task_struct* owner)
{
	pfp_domain_t *remote_cpu = remote_pfp(get_partition(owner));

	TRACE("[perform_ASAP]\n");

	raw_spin_lock(&remote_cpu->slock);

		BUG_ON(tsk_rt(owner)->task_params.cpu == tsk_rt(owner)->task_params.home);
		BUG_ON(is_queued(owner));
		
		// faccio in modo che venga prerilasciato dal job che sta effettuando busy wait
		owner->rt_param.task_params.priority = (remote_cpu->sem->prio_ceiling[get_partition(owner)]);
		preempt(remote_cpu);

	raw_spin_unlock(&remote_cpu->slock);
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

static inline struct mrsp_semaphore* mrsp_from_lock(struct litmus_lock* lock)
{
	return container_of(lock, struct mrsp_semaphore, litmus_lock);
}

int pfp_mrsp_lock(struct litmus_lock* l)
{
	struct task_struct* t = current;
	struct task_struct* owner = NULL;

	struct mrsp_semaphore *sem = mrsp_from_lock(l);
	queue_t * next;

	if (!is_realtime(t))
		return -EPERM;

	BUG_ON(sem->owner == t);

	if(init_finished == 1) {

		preempt_disable();

			spin_lock(&sem->lock);
			//TS_MRSPLOCK_START;

				__get_cpu_var(mrsp_state).cpu_ceiling = (sem->prio_ceiling[get_partition(t)]);

				queue_add_fifo(sem, t);

				next = list_entry(sem->task_queue.next,queue_t,next);

				TRACE_TASK(t, "first try\n");

				if(sem->owner == NULL && next->task == t) {
					sem->owner = t;
					t->rt_param.task_params.priority = (sem->prio_ceiling[get_partition(t)] - 2);
				} else {

					t->rt_param.task_params.priority = (sem->prio_ceiling[get_partition(t)] - 1);
					if(sem->owner != NULL)
						if(is_running(sem->owner) && is_queued(sem->owner))
							owner = sem->owner;
				}

			//TS_MRSPLOCK_END;
			spin_unlock(&sem->lock);

			if(owner) {
				mrsp_dequeue_and_migrate(get_partition(owner), get_partition(t), owner);
			}
		
		preempt_enable();

		if(sem->owner != t)
			do {
				spin_lock(&sem->lock);
				TS_MRSPLOCK_START;
				next = list_entry(sem->task_queue.next,queue_t,next);

				if(sem->owner == NULL && next->task == t) {
					sem->owner = t;
					t->rt_param.task_params.priority = (sem->prio_ceiling[get_partition(t)] - 2);
				}
				TS_MRSPLOCK_END;
				spin_unlock(&sem->lock);

			} while(sem->owner != t);

			TRACE_TASK(t, "lock!\n");
	}
	
	return 0;
}

int pfp_mrsp_unlock(struct litmus_lock* l)
{
	struct task_struct *t = current;
	struct mrsp_semaphore *sem = mrsp_from_lock(l); 
	queue_t *node;
	queue_t * next_lock_holder;
	int err = 0;

	struct task_struct* next_owner = NULL;
	int from_cpu;
	int target_cpu;

	if (!is_realtime(t))
		return -EPERM;
	
	if(init_finished == 1) {

		preempt_disable();

		spin_lock(&sem->lock);
			

			sem->owner = NULL;

			
			(*(pfp_doms[get_home(t)]->mrsp_ceiling)).cpu_ceiling = LITMUS_LOWEST_PRIORITY;
			t->rt_param.task_params.priority = t->rt_param.task_params.priority_for_restore;

			// remove task from the queue
			queue_pop(sem);

			//pfp_doms[get_home(t)]->mrsp_ceiling.cpu_ceiling = LITMUS_LOWEST_PRIORITY;
			//tsk_rt(t)->task_params.mrsp_ceiling.cpu_ceiling = LITMUS_LOWEST_PRIORITY;

			TS_MRSPUNLOCK_START;
			// Is the next lock holder queued somewhere?
			if(!list_empty(&(sem->task_queue))) {
				next_lock_holder = list_entry(sem->task_queue.next,queue_t,next);
				
				if(is_queued(next_lock_holder->task)) {

					if(get_partition(next_lock_holder->task) != get_partition(t)) {
						
						node = find_queue_entry(sem,200000);

						if(node != NULL) {
							next_owner = next_lock_holder->task;
							from_cpu = get_partition(next_owner);
							target_cpu = get_partition(node->task);
							TS_MRSPUNLOCK_END;
						}
					}
				}
			}

		spin_unlock(&sem->lock);

		if(next_owner) {
			mrsp_wake_up_next_lock_holder(from_cpu, target_cpu, next_owner);
		}

	// che sfoggerellini mi orecchi??
		if(get_partition(t) != t->rt_param.task_params.home) {
			mrsp_migrate_to_from_resource(t->rt_param.task_params.home, t);
		}
		preempt_enable();

		TRACE_TASK(t, "unlock!\n");
	}

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

	TRACE("[CLOSE]\n");

	if (owner)
		pfp_mrsp_unlock(l);

	return 0;
}

void pfp_mrsp_free(struct litmus_lock* lock)
{
	TRACE("[pfp_mrsp_free]\n");
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

static long pfp_complete_job(void)
{
	TRACE_TASK(current, "pfp_complete_job at %llu\n", litmus_clock());
	return complete_job();
}

static long pfp_allocate_lock(struct litmus_lock **lock, int type,
				 void* __user config)
{
	int err = -ENXIO;

	switch (type) {

	case MRSP_SEM:
		*lock = pfp_new_mrsp();
		if (*lock)
			err = 0;
		else
			err = -ENOMEM;
		break;
	
	};

	return err;
}

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
			//if(pfp->sem->owner != NULL) {
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
			//}
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

		if(_flag == 1 && next->rt_param.task_params.migrating == 1) {
			TS_MRSP3_START;
			TS_MRSP3_END;
			_flag = 0;
			next->rt_param.task_params.migrating = 0;
		}
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
		/* Caso base di gestione della migrazione: get_partition(prev) != smp_processor_id()
		* Oppure caso di migrazionie dato dal prerilascio del job che possedeva il lock: 
		* get_partition(prev) == MIGRATION 
		* In questo secondo caso devo cercare una cpu in cui migrare */
		int recording = 0;

		TS_MRSPMIGRATION_START;
		
		to = local_pfp;

    	tsk_rt(prev)->task_params.cpu = smp_processor_id();

		spin_lock_irqsave(&to->sem->lock, flags);
		target_node = find_queue_entry(to->sem, smp_processor_id());
		spin_unlock_irqrestore(&to->sem->lock, flags);
	
		if(target_node != NULL) {
			tsk_rt(prev)->task_params.cpu = (get_home(target_node->task));
			prev->rt_param.task_params.priority = (to->sem->prio_ceiling[tsk_rt(prev)->task_params.cpu] - 2);
			recording = 1;
		}

		to = task_pfp(prev);

		raw_spin_lock(&to->slock);

		requeue(prev, to);
		if (fp_preemption_needed(&to->ready_queue, to->scheduled))
			preempt(to);
		
		raw_spin_unlock(&to->slock);
		if(recording) {
			TS_MRSPMIGRATION_END;
#ifdef PREEMPTED_MIGRATION
			if(_flag == 0) {

				TS_MRSP2_START;
				TS_MRSP2_END;
				
				_flag++;
				prev->rt_param.task_params.migrating = 1;
			}
#endif
		}

	} else if (is_realtime(prev) && is_running(prev) && get_partition(prev) != smp_processor_id()) {

		/* Caso di LITMUS per far migrare i suoi job */

		TRACE_TASK(prev, "needs to migrate from P%d to P%d\n", smp_processor_id(), get_partition(prev));

		to = task_pfp(prev);

		raw_spin_lock(&to->slock);

		TRACE_TASK(prev, "adding to queue on P%d\n", to->cpu);
		requeue(prev, to);
		if (fp_preemption_needed(&to->ready_queue, to->scheduled))
			preempt(to);

		raw_spin_unlock(&to->slock);

	} else {

		if(init_finished == 1 && is_realtime(prev)) {
			
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

						// O non sta eseguendo nessuno il job ha priorita' pari al ceiling. Non vi possono essere job a priorita' piu' alta
						// (nel senso di meno importati), mentre job a priorita' piu' bassa non attivano i meccanismi di mrsp! 
						if(local_pfp->scheduled == NULL || (get_priority(local_pfp->scheduled) == (__get_cpu_var(mrsp_state).cpu_ceiling) - 1)) {
							// (int from_cpu, int target_cpu, struct task_struct* owner)
							owner = sem->owner;
							from_cpu = get_partition(owner);
							target_cpu = local_pfp->cpu;
						}
					}
				} 
			}

			spin_unlock(&sem->lock);

			// cpu_again_available_for_migration(int from_cpu, int target_cpu, struct task_struct* owner)
			if(owner != NULL) {
				preempt_disable();
				cpu_again_available_for_migration(from_cpu, target_cpu, owner);
				preempt_enable();
			}

		}
	}
}

void pfp_release_at(struct task_struct *t, lt_t start)
{
	//init_finished = 1;

	t->rt_param.task_params.migrating = -1;

	TRACE_TASK(t, " released_task \n");

	tsk_rt(t)->task_params.next = (queue_t *) kmalloc(sizeof(queue_t), GFP_KERNEL);
  	tsk_rt(t)->task_params.next->task = t;

  	pfp_doms[local_pfp->cpu]->mrsp_ceiling = (&(__get_cpu_var(mrsp_state)));

	/*queue_t * next;
  next = (queue_t *) kmalloc(sizeof(queue_t), GFP_KERNEL);
  next->task = task;*/

	/*t->rt_param.task_params.last_cpu = -1;
	t->rt_param.task_params.next_cpu = -1;*/
}

static long pfp_admit_task(struct task_struct* tsk)
{
	TRACE_TASK(tsk, "[pfp_admit_task] Task priority %d\n", get_priority(tsk));
	if (task_cpu(tsk) == tsk->rt_param.task_params.cpu &&
	    litmus_is_valid_fixed_prio(get_priority(tsk)))
		return 0;
	else
		return -EINVAL;
}

static long pfp_activate_plugin(void)
{
	int cpu;

	for_each_online_cpu(cpu) {

		mrsp_init_state(&per_cpu(mrsp_state, cpu));
		pfp_doms[cpu] = remote_pfp(cpu);
	}

	init_finished = 0;
	_flag = 0;

	return 0;
}

static long pfp_deactivate_plugin(void)
{

	int cpu;

	if(local_pfp->sem != NULL) {
			
		TRACE("[RESET MRSP]\n");

		for (cpu = 0; cpu < NR_CPUS; cpu++)
			local_pfp->sem->prio_ceiling[cpu] = (LITMUS_MAX_PRIORITY - 2);
	}

	for_each_online_cpu(cpu) {
		pfp_doms[cpu]->sem = NULL;
	}

	init_finished = 0;
	_flag = 0;

	return 0;	
}



/*	Plugin object	*/
static struct sched_plugin pfp_plugin __cacheline_aligned_in_smp = {
	.plugin_name		= "PSN-EDF", //P-FP
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


static int __init init_pfp(void)
{
	int i;

	/* We do not really want to support cpu hotplug, do we? ;)
	 * However, if we are so crazy to do so,
	 * we cannot use num_online_cpu()
	 */
	for (i = 0; i < num_online_cpus(); i++) {
		pfp_domain_init(remote_pfp(i), i);
	}
	return register_sched_plugin(&pfp_plugin);
}

module_init(init_pfp);
