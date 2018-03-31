
/*

https://blog.csdn.net/chen98765432101/article/details/69367633

环形队列 无锁队列
# 无符号二进制数的环性
有符号数 原码是人类可读的书写方式 反码是历史进程中的中间产物 缺陷是有 +0 -0 两个数存在
补码是计算机对有符号数的表达方式 不易读
# Compare And Set CAS  CPU 上 非lock 的同步机制

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_log.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_memcpy.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_arp.h>
#include <rte_spinlock.h>
#include <rte_ring.h>
#include <rte_malloc.h>

#include "header.h"

struct context
{
    uint64_t count;
    uint64_t dequeue_seq;
};

int enqueue(struct context * ctx, struct peek_ring * ring)
{
    int rc;
    // malloc 对方读取不到
    // rte_malloc 另一个进程能读取到
    uint64_t * p = rte_zmalloc("enqueue_datas",sizeof(uint64_t),0);
    if (!p)
    {
        // fprintf(stdout, "Fail rte_zmalloc\n"); fflush(stdout);
        return -1;
    }
    *p = ctx->count;
    rc = rte_ring_enqueue(ring->rx_queue, p);
    
    if (rc < 0)
    {
        //fprintf(stderr, " Fail rte_ring_enqueue() at %u\n", ctx->count);
        //fflush(stderr);
        rte_free(p);
        return rc;
    }
    ctx->count += 1;
    return 0;
}

void dequeue(struct context * ctx, struct peek_ring * ring)
{
    pthread_mutex_lock(&ring->mutex);
    uint32_t can_dequeue_size = ring->cons_cur - ring->rx_queue->cons.tail;
    pthread_mutex_unlock(&ring->mutex);
    int rc;
    uint32_t i;
    uint64_t * obj=0;

    //can_dequeue_size = rte_ring_count(ring->rx_queue);
    if (can_dequeue_size == 0)
    {
        //fprintf(stdout, "dequeue can size = %u \n", can_dequeue_size); fflush(stdout);
    }
    //fprintf(stdout, "dequeue can size = %u \n", can_dequeue_size); fflush(stdout);

    for (i=0;i<can_dequeue_size;i+=1)
    {
        rc = rte_ring_dequeue(ring->rx_queue, (void**)&obj);
        if (rc < 0)
        {
            fprintf(stderr, "Fail rte_ring_dequeue()\n");
            fflush(stderr);
        }
        if (obj)
        {
            //printf(" dequeue %d ", *obj);
            if (ctx->dequeue_seq != *obj)
            {
                fprintf(stdout, "dequeue invalid ctx->dequeue_seq = %lld dequeue = %lld\n"
                    ,(long long) ctx->dequeue_seq, (long long)*obj); fflush(stdout);
            }
            rte_free(obj);
            ctx->dequeue_seq += 1;
        }
    }
}

// 可以不用 master slave 共享吗？
#define MASTER_CREATE_RING_NAME "test_ring"

int main(int argc, const char * argv[])
{
    int rc;
    const char ** argv2 = calloc(argc + 2, sizeof(const char *));
    int i;
    for (i = 0; i < argc; i += 1)
    {
        argv2[i] = argv[i];
    }
    // primary secondary auto
    argv2[i] = "-l"; i += 1;
    argv2[i] = "0-0";
    rc = rte_eal_init(argc+2, (char**)argv2);
    free(argv2);
    if (rc < 0)
    {
        rte_panic("Fail rte_eal_init\n");
    }

    struct peek_ring * ring;
    const struct rte_memzone * zone;

    zone = rte_memzone_reserve(MASTER_MEMORY_ZONE_NAME,
        sizeof(struct peek_ring), rte_socket_id(), RTE_MEMZONE_2MB);

    if (zone == 0)
    {
        rte_panic("Fail rte_memzone_reserve()");
    }
    ring = zone->addr;

    memset(ring, 0, sizeof(struct peek_ring));
    ring->rx_queue = rte_ring_create(MASTER_CREATE_RING_NAME, 1 << 4, 0, 0);
    if (ring->rx_queue == NULL)
    {
        rte_panic("Fail rte_ring_create()");
    }

    pthread_mutex_init(&ring->mutex,0);
    struct context ctx;
    memset(&ctx, 0, sizeof(struct context));

    
    ring->cons_cur = ring->rx_queue->cons.head;

    for (;;)
    {
        rc = enqueue(&ctx, ring);
        if (rc < 0)
        {
            //rte_ring_list_dump(stdout);
            //break;
            //sleep(1);
        }
        //usleep(10);
        //sleep(1);
       dequeue(&ctx,ring);

        
    }

    rte_ring_free(ring->rx_queue);
    rte_memzone_free(zone);

    return 0;
}
