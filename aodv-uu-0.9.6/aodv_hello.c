/*****************************************************************************
 *
 * Copyright (C) 2001 Uppsala University and Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Erik Nordström, <erik.nordstrom@it.uu.se>
 *          
 *
 *****************************************************************************/

#ifdef NS_PORT
#include "ns-2/aodv-uu.h"



#else
#include <netinet/in.h>
#include <string.h>
#include "aodv_hello.h"
#include "aodv_timeout.h"
#include "aodv_rrep.h"
#include "aodv_rreq.h"
#include "routing_table.h"
#include "timer_queue.h"
#include "params.h"
#include "aodv_socket.h"
#include "defs.h"
#include "debug.h"


extern int unidir_hack, receive_n_hellos, hello_jittering, optimized_hellos;
static struct timer hello_timer;

int n_index = -1;
int nn_index = -1;
int nn[20] = {0};

#endif



/* #define DEBUG_HELLO */

// 输出调试信息
void NS_CLASS p_n(int i){
	//int t;
	//for(t=1; t<20; t++){
	//	if(strlen(n_info[t].cnip) != 0 && strlen(n_info[t].nbip) != 0){
	//		fprintf(stderr, "N_INFO[%d]::  current ip=%s, neighbor ip=%s, channel=%d, time=%ld.%ld, state=%d\n",
	//			t, n_info[t].cnip, n_info[t].nbip, n_info[t].cn, (n_info[t].time).tv_sec, (n_info[t].time).tv_usec, n_info[t].state);
	//	}
	//}
}

// 查找结构体中是否已经记录了这条记录
int NS_CLASS find_n(char* cip, char* nip, int c){
	int i;
	for(i = 0; i < 20; i++){
		if(strcmp(n_info[i].cnip, cip) == 0 && strcmp(n_info[i].nbip, nip) == 0 && n_info[i].cn == c){
			//fprintf(stderr, "FIND_N:: at n_info[%d]:%s -> %d -> %s already exist.\n", i, cip, c, nip);
			return i;
		}	
	}
	// 不存在这个项
	return -1;
}

// 更新结构体中已经存在的项
int NS_CLASS update_n(int i){
	struct timeval now;
	gettimeofday(&now, NULL);
	// 状态:之前时刻已经存在由1变为0
	n_info[i].state = 0;
	// 更新计时器的时间
	(n_info[i].time).tv_sec = now.tv_sec;
	(n_info[i].time).tv_usec = now.tv_usec;
}


// 在结构体中插入一条新的记录
int NS_CLASS insert_n(char* cip, char* nip, int c, struct timeval t, int s){
	//fprintf(stderr, "------------------------* insert into n_info *------------------------\n");
	//fprintf(stderr, "before insert:\n");
	//p_n(0);
	//fprintf(stderr, "start insert:\n");
	int i = 0;	
	n_index++;
	//fprintf(stderr, "curent n_index is: %d\n", n_index);
	strcpy(n_info[n_index].cnip, cip);
	strcpy(n_info[n_index].nbip, nip);
	n_info[n_index].cn = c;
	(n_info[n_index].time).tv_sec = t.tv_sec;
	(n_info[n_index].time).tv_usec = t.tv_usec;
	n_info[n_index].state = s;
	//fprintf(stderr, "after insert:\n");
	//p_n(0);
	//fprintf(stderr, "-------------------------------------------------------------------\n");
	return n_index; 
}

long NS_CLASS hello_jitter()
{
    if (hello_jittering) {
#ifdef NS_PORT
	return (long) (((float) Random::integer(RAND_MAX + 1) / RAND_MAX - 0.5)
		       * JITTER_INTERVAL);
#else
	return (long) (((float) random() / RAND_MAX - 0.5) * JITTER_INTERVAL);
#endif
    } else
	return 0;
}

void NS_CLASS hello_start()
{
    if (hello_timer.used)
	return;

    gettimeofday(&this_host.fwd_time, NULL);
    DEBUG(LOG_DEBUG, 0, "Starting to send HELLOs!");
    timer_init(&hello_timer, &NS_CLASS hello_send, NULL);

    hello_send(NULL);
}

void NS_CLASS hello_stop()
{
    DEBUG(LOG_DEBUG, 0,
	  "No active forwarding routes - stopped sending HELLOs!");
    timer_remove(&hello_timer);
}

void NS_CLASS hello_send(void *arg)
{

    RREP *rrep;
    AODV_ext *ext = NULL;
    u_int8_t flags = 0;
    struct in_addr dest;
    long time_diff, jitter;
    struct timeval now;
    int ifindex = 0;
    int msg_size = RREP_SIZE;
    int i,j,k;

	int l,r;
	int sum_0 = 0;
	int sum_1 = 0;
	int sum_2 = 0;
	float ncr; 
	float ucn, nbr, iir;
	float result;
	u_int16_t final;

    gettimeofday(&now, NULL);

    if (optimized_hellos &&
	timeval_diff(&now, &this_host.fwd_time) > ACTIVE_ROUTE_TIMEOUT) {
	hello_stop();
	return;
    }

    time_diff = timeval_diff(&now, &this_host.bcast_time);
    jitter = hello_jitter();

    /* This check will ensure we don't send unnecessary hello msgs, in case
       we have sent other bcast msgs within HELLO_INTERVAL */
    if (time_diff >= HELLO_INTERVAL) {

	for (i = 0; i < MAX_NR_INTERFACES; i++) {
	    if (!DEV_NR(i).enabled)
		continue;
#ifdef DEBUG_HELLO
	    DEBUG(LOG_DEBUG, 0, "sending Hello to 255.255.255.255");
#endif

	// 统计三种状态的项目的个数
	// fprintf(stderr, "curent ip(DEV_NR(i).ipaddr) is %s\n", ip_to_str(DEV_NR(i).ipaddr));
	for(l = 1; l < 20; l++){
		if(strcmp(n_info[l].cnip, ip_to_str(DEV_NR(i).ipaddr)) == 0 && n_info[l].state == 0){
			sum_0++; //fprintf(stderr, "neighbor ip: %s\n", n_info[l].nbip);
		} 
		if(strcmp(n_info[l].cnip, ip_to_str(DEV_NR(i).ipaddr)) == 0 && n_info[l].state == 1){
			sum_1++; //fprintf(stderr, "neighbor ip: %s\n", n_info[l].nbip);
		} 
		if(strcmp(n_info[l].cnip, ip_to_str(DEV_NR(i).ipaddr)) == 0 && n_info[l].state == 2){
			sum_2++;
		} 
	}

	fprintf(stderr, ">>>[%s]:sum_0=%d ", ip_to_str(DEV_NR(i).ipaddr), sum_0);
	fprintf(stderr, "sum_1=%d ", sum_1);
	fprintf(stderr, "sum_2=%d ", sum_2);

	// 计算邻居节点变化，calculate neighbor change rate
	sum_0 + sum_1 + sum_2 == 0 ? (ncr = -1) : (ncr = sum_0 / (sum_1 + sum_2 + sum_0));
	fprintf(stderr, "| ncr = %.2lf ", ncr);
	// 计算可用信道数，calculate usable channels number
	ucn = sum_0 + sum_1;
	// 归一化
	(ucn / 10 > 1 ) ? (ucn = 1) : (ucn = ucn / 5);
	(ucn == 0) ? (ucn = -1) : (ucn = ucn);

	fprintf(stderr, "| ucn = %.2lf", ucn);
	// 计算信号干扰，calculate neighbor break rate

	sum_0 + sum_1 + sum_2 == 0 ? (nbr = -1) : (nbr = sum_2 / (sum_1 + sum_2 + sum_0));
	nbr = 1 - nbr;
	fprintf(stderr, "| nbr = %.2lf", nbr);
	// 计算流间干扰，calculate interflow interference rate
	iir = 1;
	// 这里缺一个公式，之后补上 
	fprintf(stderr, "| iir = %.2lf", iir);
	// 计算节点的可用性参数
	(ncr == -1 || ucn == -1) ? (result = -1) : (result = ncr * ucn * nbr * iir);
	fprintf(stderr, "| result = %.2lf", result);
	if(result < 0) final = 0;
	else if(result >= 0   && result < 0.2) final = 1;
	else if(result >= 0.2 && result < 0.4) final = 2;
	else final = 3;
	fprintf(stderr, "| final = %d\n", final);

	strcpy(n_info[0].cnip, ip_to_str(DEV_NR(i).ipaddr));
	strcpy(n_info[0].nbip, ip_to_str(DEV_NR(i).ipaddr));
	

	    rrep = rrep_create(flags, 0, 0, DEV_NR(i).ipaddr,
			       this_host.seqno,
			       DEV_NR(i).ipaddr,
			       ALLOWED_HELLO_LOSS * HELLO_INTERVAL);
	rrep->res1 = final;
	//fprintf(stderr, "in hello message: %d\n", rrep->res1);
	
		char current[20];
		strcpy(current, ip_to_str(DEV_NR(i).ipaddr));

	    /* Assemble a RREP extension which contain our neighbor set... */
	    if (unidir_hack) {
		int i;
		
		if (ext)
		    ext = AODV_EXT_NEXT(ext);
		else
		    ext = (AODV_ext *) ((char *) rrep + RREP_SIZE);

		ext->type = RREP_HELLO_NEIGHBOR_SET_EXT;
		ext->length = 0;

		for (i = 0; i < RT_TABLESIZE; i++) {
			list_t *pos;
			
		    list_foreach(pos, &rt_tbl.tbl[i]) {
			rt_table_t *rt = (rt_table_t *) pos;
			/* If an entry has an active hello timer, we assume
			   that we are receiving hello messages from that
			   node... */
			if (rt->hello_timer.used) {

		
#ifdef DEBUG_HELLO
			    DEBUG(LOG_INFO, 0,
				  "Adding %s to hello neighbor set ext",
				  ip_to_str(rt->dest_addr));
#endif

		//fprintf(stderr, "*********-------------------- Add neighbor --------------------------*********\n");
		//fprintf(stderr, "* before add neighbor:\n");
		//p_n(0);
		//fprintf(stderr, "  Adding %s to hello neighbor\n", ip_to_str(rt->dest_addr));
		// 获取当前时间
		gettimeofday(&now, NULL);
		//fprintf(stderr, "  time:%ld.%ld\n", now.tv_sec, now.tv_usec);
		int find = find_n(current, ip_to_str(rt->dest_addr), ifindex);
		if(find == -1){
			//fprintf(stderr, "  this information do not exist, start to insert it.\n");
			insert_n(current, ip_to_str(rt->dest_addr), ifindex, now, 1);
		}else{
			//fprintf(stderr, "  this information do exist, start to update.\n");
			update_n(find);
		}
		//fprintf(stderr, "-----------------------------------------------------------------------------\n");

			    memcpy(AODV_EXT_DATA(ext), &rt->dest_addr,
				   sizeof(struct in_addr));
			    ext->length += sizeof(struct in_addr);
			}
		    }
		}
		if (ext->length)
		    msg_size = RREP_SIZE + AODV_EXT_SIZE(ext);
	    }
	    dest.s_addr = AODV_BROADCAST;
	    aodv_socket_send((AODV_msg *) rrep, dest, msg_size, 1, &DEV_NR(i));
	}

	timer_set_timeout(&hello_timer, HELLO_INTERVAL + jitter);
    } else {
	if (HELLO_INTERVAL - time_diff + jitter < 0)
	    timer_set_timeout(&hello_timer,
			      HELLO_INTERVAL - time_diff - jitter);
	else
	    timer_set_timeout(&hello_timer,
			      HELLO_INTERVAL - time_diff + jitter);
    }
}


/* Process a hello message */
void NS_CLASS hello_process(RREP * hello, int rreplen, unsigned int ifindex)
{
	// fprintf(stderr, "-------------------------------- hello process ------------------------------\n");
    // fprintf(stderr, "- Processing Hello message.\n");
    u_int32_t hello_seqno, timeout, hello_interval = HELLO_INTERVAL;
    u_int8_t state, flags = 0;
    struct in_addr ext_neighbor, hello_dest;

	int count = 0;
	int p = 1;
	int sum = 0;
	//int num = 0;
	char adr1[16]; int c1 = 0;
	char adr2[16]; int c2 = 0;
	char adr3[16]; int c3 = 0;

	struct in_addr hello_orig;
	struct in_addr _adr1, _adr2, _adr3;

	HELLO_ack *hello_ack = NULL;
	 
    rt_table_t *rt;
    AODV_ext *ext = NULL;
    int i;
	int t;
    struct timeval now;

    gettimeofday(&now, NULL);
    hello_dest.s_addr = hello->dest_addr;

	//fprintf(stderr, "destaddr:%d\n", hello->dest_addr);
    hello_seqno = ntohl(hello->dest_seqno);	

    rt = rt_table_find(hello_dest);

    if (rt)
	flags = rt->flags;

    if (unidir_hack)
	flags |= RT_UNIDIR;

    /* Check for hello interval extension: */
    ext = (AODV_ext *) ((char *) hello + RREP_SIZE);

    while (rreplen > (int) RREP_SIZE) {
	switch (ext->type) {
	case RREP_HELLO_INTERVAL_EXT:
	    if (ext->length == 4) {
		memcpy(&hello_interval, AODV_EXT_DATA(ext), 4);
		hello_interval = ntohl(hello_interval);
#ifdef DEBUG_HELLO
		DEBUG(LOG_INFO, 0, "Hello extension interval=%lu!",
		      hello_interval);
#endif

	    } else
		alog(LOG_WARNING, 0,
		     __FUNCTION__, "Bad hello interval extension!");
	    break;
	case RREP_HELLO_NEIGHBOR_SET_EXT:

#ifdef DEBUG_HELLO
	    DEBUG(LOG_INFO, 0, "RREP_HELLO_NEIGHBOR_SET_EXT");
#endif
	    for (i = 0; i < ext->length; i = i + 4) {
		ext_neighbor.s_addr =
		    *(in_addr_t *) ((char *) AODV_EXT_DATA(ext) + i);

		if (ext_neighbor.s_addr == DEV_IFINDEX(ifindex).ipaddr.s_addr)
		    flags &= ~RT_UNIDIR;
	    }
	    break;
	default:
	    alog(LOG_WARNING, 0, __FUNCTION__,
		 "Bad extension!! type=%d, length=%d", ext->type, ext->length);
	    ext = NULL;
	    break;
	}
	if (ext == NULL)
	    break;

	rreplen -= AODV_EXT_SIZE(ext);
	ext = AODV_EXT_NEXT(ext);
    }

#ifdef DEBUG_HELLO
    DEBUG(LOG_DEBUG, 0, "rcvd HELLO from %s, seqno %lu",
	  ip_to_str(hello_dest), hello_seqno);
#endif
    /* This neighbor should only be valid after receiving 3
       consecutive hello messages... */
    if (receive_n_hellos){
		state = INVALID;
// 邻居节点状态变为不可用，更新结构体
// 所有有这个ip地址的项状态都设置为2
	for(t = 0; t < 20; t++){
		if(strcmp(n_info[t].cnip, ip_to_str(hello_dest)) == 0 
			|| strcmp(n_info[t].nbip, ip_to_str(hello_dest)) == 0){
			// 更新计时器时间
			gettimeofday(&now, NULL);
			(n_info[t].time).tv_sec = now.tv_sec;
			(n_info[t].time).tv_usec = now.tv_usec;
			// 更新状态
			n_info[t].state = 2;
		}
	}
	}
	
    else{
		state = VALID;
	}
	

    timeout = ALLOWED_HELLO_LOSS * hello_interval + ROUTE_TIMEOUT_SLACK;

    if (!rt) {
	/* No active or expired route in the routing table. So we add a
	   new entry... */

	rt = rt_table_insert(hello_dest, hello_dest, 1,
			     hello_seqno, timeout, state, flags, ifindex);

	if (flags & RT_UNIDIR) {
	    DEBUG(LOG_INFO, 0, "%s new NEIGHBOR, link UNI-DIR",
		  ip_to_str(rt->dest_addr));
	} else {
	    DEBUG(LOG_INFO, 0, "%s new NEIGHBOR!", ip_to_str(rt->dest_addr));
	}
	rt->hello_cnt = 1;

    } else {

	if ((flags & RT_UNIDIR) && rt->state == VALID && rt->hcnt > 1) {

	    goto hello_update;
	}

	if (receive_n_hellos && rt->hello_cnt < (receive_n_hellos - 1)) {
	    if (timeval_diff(&now, &rt->last_hello_time) <
		(long) (hello_interval + hello_interval / 2))
		rt->hello_cnt++;
	    else
		rt->hello_cnt = 1;

	    memcpy(&rt->last_hello_time, &now, sizeof(struct timeval));
	    return;
	}
	rt_table_update(rt, hello_dest, 1, hello_seqno, timeout, VALID, flags);
    }
  hello_update:

    hello_update_timeout(rt, &now, ALLOWED_HELLO_LOSS * hello_interval);
/* ============================================================================== */
// 向hello消息的源节点发送hello_ack消息

	//fprintf(stderr, "------------------------- Begin to send hello_ack ----------------------\n");	
	//fprintf(stderr, " n_info:\n");
	//p_n(0);
	adr1[0]='\0'; adr2[0]='\0'; adr3[0]='\0'; 
	c1 = 0; c2 = 0; c3 = 0;

	char current[20];
	//hello_orig.s_addr = 9;
	if(strlen(n_info[0].cnip) != 0){
		strcpy(current, n_info[0].cnip);
	    hello_orig.s_addr = (int)current[6]-(int)('0');
		if((int)current[7] >= (int)('0') && (int)current[7] <= (int)('9')){
			hello_orig.s_addr = hello_orig.s_addr * 10 + (int)current[7]-(int)('0');
		}
	} 
	//fprintf(stderr, "hello_orig:%s\n", ip_to_str(hello_orig));
	// 统计邻居个数
	count = 0;
	for(t = 1; t < 20; t++){
		if(strlen(n_info[t].nbip) != 0 && strcmp(n_info[t].nbip, ip_to_str(hello_dest)) != 0) count++;
	}
	//fprintf(stderr, "number of neghbors:%d\n", count);

	// 创建hello_ack消息
	hello_ack = hello_ack_create(hello_dest, hello_orig, count);
	if(hello_orig.s_addr <= 100) hello_ack_send(hello_ack, ifindex);

/* ============================================================================== */
    return;
}


#define HELLO_DELAY 50		/* The extra time we should allow an hello
				   message to take (due to processing) before
				   assuming lost . */

NS_INLINE void NS_CLASS hello_update_timeout(rt_table_t * rt,
					     struct timeval *now, long time)
{
    timer_set_timeout(&rt->hello_timer, time + HELLO_DELAY);
    memcpy(&rt->last_hello_time, now, sizeof(struct timeval));
}
