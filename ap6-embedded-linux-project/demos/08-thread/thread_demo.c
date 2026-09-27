/*
 * thread_demo.c - thread, race condition, mutex, condition variable
 *                 (Bao cao muc 8.1 - 8.5).
 *
 *   1. Thread chia se bien toan cuc -> race condition khi khong khoa
 *   2. Mutex bao ve critical section -> ket qua dung
 *   3. Producer - consumer bang condition variable (thay cho polling)
 *   4. Thu tu lock nhat quan de tranh deadlock
 *
 * Bien dich: gcc -Wall -pthread -o thread_demo thread_demo.c
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NTHREADS	4
#define NLOOPS		200000
#define QUEUE_SIZE	8
#define NITEMS		20

/* ---------- 1 & 2: race condition va mutex ---------- */

static long g_unsafe_counter;
static long g_safe_counter;
static pthread_mutex_t g_counter_lock = PTHREAD_MUTEX_INITIALIZER;

static void *counter_thread(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < NLOOPS; i++) {
		/* Khong khoa: "counter++" gom load - add - store, hai thread
		 * xen ke nhau se lam mat mot lan cap nhat (muc 8.3). */
		g_unsafe_counter++;

		pthread_mutex_lock(&g_counter_lock);
		g_safe_counter++;
		pthread_mutex_unlock(&g_counter_lock);
	}
	return NULL;
}

static void muc_1_2_race(void)
{
	pthread_t th[NTHREADS];
	long expected = (long)NTHREADS * NLOOPS;
	int i;

	printf("1+2) Race condition va mutex (%d thread x %d vong)\n",
	       NTHREADS, NLOOPS);

	for (i = 0; i < NTHREADS; i++)
		pthread_create(&th[i], NULL, counter_thread, NULL);
	for (i = 0; i < NTHREADS; i++)
		pthread_join(th[i], NULL);

	printf("   gia tri mong doi          : %ld\n", expected);
	printf("   khong khoa (co race)      : %ld  (mat %ld lan cap nhat)\n",
	       g_unsafe_counter, expected - g_unsafe_counter);
	printf("   co mutex bao ve           : %ld\n", g_safe_counter);
	printf("   -> ket qua sai khong on dinh giua cac lan chay, dung loai\n"
	       "      loi rat kho tai hien tren thiet bi that.\n\n");
}

/* ---------- 3: producer - consumer voi condition variable ---------- */

struct queue {
	int buf[QUEUE_SIZE];
	int head, tail, count;
	int closed;
	pthread_mutex_t lock;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
};

static void queue_init(struct queue *q)
{
	memset(q, 0, sizeof(*q));
	pthread_mutex_init(&q->lock, NULL);
	pthread_cond_init(&q->not_empty, NULL);
	pthread_cond_init(&q->not_full, NULL);
}

static void queue_destroy(struct queue *q)
{
	pthread_mutex_destroy(&q->lock);
	pthread_cond_destroy(&q->not_empty);
	pthread_cond_destroy(&q->not_full);
}

static void queue_push(struct queue *q, int v)
{
	pthread_mutex_lock(&q->lock);
	/* Luon dung while, khong dung if: thread co the bi danh thuc gia
	 * (spurious wakeup) hoac bi thread khac giat mat cho (muc 8.4). */
	while (q->count == QUEUE_SIZE)
		pthread_cond_wait(&q->not_full, &q->lock);

	q->buf[q->head] = v;
	q->head = (q->head + 1) % QUEUE_SIZE;
	q->count++;
	pthread_cond_signal(&q->not_empty);
	pthread_mutex_unlock(&q->lock);
}

static int queue_pop(struct queue *q, int *out)
{
	pthread_mutex_lock(&q->lock);
	while (q->count == 0 && !q->closed)
		pthread_cond_wait(&q->not_empty, &q->lock);

	if (q->count == 0 && q->closed) {
		pthread_mutex_unlock(&q->lock);
		return -1;
	}
	*out = q->buf[q->tail];
	q->tail = (q->tail + 1) % QUEUE_SIZE;
	q->count--;
	pthread_cond_signal(&q->not_full);
	pthread_mutex_unlock(&q->lock);
	return 0;
}

static void queue_close(struct queue *q)
{
	pthread_mutex_lock(&q->lock);
	q->closed = 1;
	pthread_cond_broadcast(&q->not_empty);
	pthread_mutex_unlock(&q->lock);
}

static struct queue g_queue;
static long g_consumed_sum;

static void *producer(void *arg)
{
	int i;

	(void)arg;
	for (i = 1; i <= NITEMS; i++) {
		queue_push(&g_queue, i);
		usleep(2000);
	}
	queue_close(&g_queue);
	return NULL;
}

static void *consumer(void *arg)
{
	long id = (long)arg;
	long local = 0;
	int v;

	while (queue_pop(&g_queue, &v) == 0)
		local += v;

	printf("   consumer %ld tong cong %ld\n", id, local);
	pthread_mutex_lock(&g_counter_lock);
	g_consumed_sum += local;
	pthread_mutex_unlock(&g_counter_lock);
	return NULL;
}

static void muc_3_producer_consumer(void)
{
	pthread_t prod, cons[2];
	long i;

	printf("3) Producer - consumer bang condition variable\n");
	queue_init(&g_queue);

	pthread_create(&prod, NULL, producer, NULL);
	for (i = 0; i < 2; i++)
		pthread_create(&cons[i], NULL, consumer, (void *)(i + 1));

	pthread_join(prod, NULL);
	for (i = 0; i < 2; i++)
		pthread_join(cons[i], NULL);

	printf("   tong tat ca item = %ld (mong doi %d)\n", g_consumed_sum,
	       NITEMS * (NITEMS + 1) / 2);
	printf("   -> consumer NGU tren condvar, khong quay vong polling,\n"
	       "      tiet kiem CPU tren thiet bi nhung.\n\n");
	queue_destroy(&g_queue);
}

/* ---------- 4: thu tu lock de tranh deadlock ---------- */

static pthread_mutex_t lock_a = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lock_b = PTHREAD_MUTEX_INITIALIZER;

/* Quy uoc: MOI thread deu lay lock_a truoc, lock_b sau. Neu mot thread lay
 * nguoc thu tu, hai thread se cho nhau vong tron -> deadlock (muc 8.4). */
static void *worker_ab(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < 1000; i++) {
		pthread_mutex_lock(&lock_a);
		pthread_mutex_lock(&lock_b);
		g_safe_counter++;
		pthread_mutex_unlock(&lock_b);
		pthread_mutex_unlock(&lock_a);
	}
	return NULL;
}

static void muc_4_deadlock(void)
{
	pthread_t t1, t2;

	printf("4) Tranh deadlock bang thu tu lock nhat quan\n");
	pthread_create(&t1, NULL, worker_ab, NULL);
	pthread_create(&t2, NULL, worker_ab, NULL);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	printf("   hai thread cung lay lock theo thu tu A -> B: khong deadlock\n");
	printf("   -> neu doi mot thread thanh B -> A, chuong trinh se treo;\n"
	       "      dung 'gdb -p <pid>' + 'thread apply all bt' de xem.\n");
}

int main(void)
{
	printf("=== Thread trong Linux ===\n\n");
	muc_1_2_race();
	muc_3_producer_consumer();
	muc_4_deadlock();
	return 0;
}
