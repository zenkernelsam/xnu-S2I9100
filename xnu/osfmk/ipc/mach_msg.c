/*
 * Copyright (c) 2000-2007 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * @OSF_COPYRIGHT@
 */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 * NOTICE: This file was modified by McAfee Research in 2004 to introduce
 * support for mandatory and extensible security protections.  This notice
 * is included in support of clause 2.2 (b) of the Apple Public License,
 * Version 2.0.
 * Copyright (c) 2005 SPARTA, Inc.
 */
/*
 */
/*
 *	File:	ipc/mach_msg.c
 *	Author:	Rich Draves
 *	Date:	1989
 *
 *	Exported message traps.  See mach/message.h.
 */

#include <mach/mach_types.h>
#include <mach/kern_return.h>
#include <mach/port.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/mach_traps.h>

#include <kern/kern_types.h>
#include <kern/assert.h>
#include <kern/counters.h>
#include <kern/cpu_number.h>
#include <kern/ipc_kobject.h>
#include <kern/ipc_mig.h>
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/lock.h>
#include <kern/sched_prim.h>
#include <kern/exception.h>
#include <kern/misc_protos.h>
#include <kern/kalloc.h>
#include <kern/processor.h>
#include <kern/syscall_subr.h>

#include <vm/vm_map.h>

#include <ipc/ipc_types.h>
#include <ipc/ipc_kmsg.h>
#include <ipc/ipc_mqueue.h>
#include <ipc/ipc_object.h>
#include <ipc/ipc_notify.h>
#include <ipc/ipc_port.h>
#include <ipc/ipc_pset.h>
#include <ipc/ipc_space.h>
#include <ipc/ipc_entry.h>

#include <machine/machine_routines.h>
#include <security/mac_mach_internal.h>

#include <sys/kdebug.h>
#if BOARD_CONFIG_QSD8250_LEO
#include <mach/mach_time.h>
#include <libsa/string.h>
extern int proc_pid(struct proc *);
extern int proc_pidversion(struct proc *);
extern char *proc_name_address(void *);
#include "IOS7LeoPreferencesRPC060.h"
#endif
#if BOARD_CONFIG_ARMPBA8
#include <pexpert/pexpert.h>
#include <libsa/string.h>
#include <libkern/OSAtomic.h>
extern int proc_selfpid(void);
extern void proc_name(int pid, char *buffer, int size);

/* Header-only protocol boundaries. These IDs do not identify an XPC command. */
static volatile UInt32 ios7lab_xpc_lines, ios7lab_asl_lines;
static int ios7lab_boundary_trace(mach_msg_header_t *header)
{
    int enabled = 0, kind;
    char name[32] = {0};
    if (!header) return 0;
    if (header->msgh_id == 0x10000000 || header->msgh_id == 0x77303074)
        kind = 1;
    else if (header->msgh_id == 118)
        kind = 2;
    else return 0;
    if (!PE_parse_boot_argn("ios7lab_vmabi", &enabled, sizeof(enabled)) || enabled != 1)
        return 0;
    proc_name(proc_selfpid(), name, sizeof(name));
    if (kind == 1) {
        if (strcmp(name, "lsd") && strcmp(name, "installd")) return 0;
    } else {
        if (strcmp(name, "syslogd") && strcmp(name, "mstreamd") &&
            strcmp(name, "SpringBoard") && strcmp(name, "backboardd")) return 0;
    }
    return kind;
}
static void ios7lab_boundary_log(int kind, const char *stage, int id,
    mach_msg_size_t internal_size, mach_msg_size_t user_size,
    mach_msg_bits_t bits, mach_msg_option_t option, mach_msg_return_t result)
{
    volatile UInt32 *counter = kind == 1 ? &ios7lab_xpc_lines : &ios7lab_asl_lines;
    UInt32 limit = kind == 1 ? 32 : 16, used = *counter;
    /* One CAS attempt: strict line cap, drop a contended diagnostic, never wait. */
    if (used >= limit || !OSCompareAndSwap(used, used + 1, counter)) return;
    printf("IOS7LAB %s %s seq=%u pid=%d tid=%llu id=%d internal-size=%u converted-user-size=%u bits=%x option=%x result=%x\n",
        kind == 1 ? "XPCIPC" : "ASLIPC", stage, used + 1, proc_selfpid(),
        (unsigned long long)thread_tid(current_thread()), id, internal_size,
        user_size, bits, option, result);
}
/* Receive-only metadata from normalized kernel descriptors, before copyout. */
static volatile UInt32 ios7lab_xpc_descriptor_lines;
static void ios7lab_xpc_descriptor_log(ipc_kmsg_t kmsg)
{
    mach_msg_header_t *header;
    mach_msg_body_t *body;
    mach_msg_descriptor_t *descriptors;
    vm_offset_t begin, address;
    mach_msg_size_t available, base, count, inspected, i;
    unsigned int types[4] = {255, 255, 255, 255};
    unsigned int dispositions[4] = {0, 0, 0, 0};
    unsigned int scalars[4] = {0, 0, 0, 0};
    UInt32 used;
    char name[32] = {0};
    if (!kmsg || !kmsg->ikm_header) return;
    /* ikm_size excludes struct ipc_kmsg; headers may sit inside its buffer. */
    begin = (vm_offset_t)(kmsg + 1);
    address = (vm_offset_t)kmsg->ikm_header;
    if (address < begin || address - begin > kmsg->ikm_size) return;
    available = kmsg->ikm_size - (mach_msg_size_t)(address - begin);
    if (available < sizeof(mach_msg_header_t)) return;
    header = kmsg->ikm_header;
    if (header->msgh_id != 0x10000000 ||
        !(header->msgh_bits & MACH_MSGH_BITS_COMPLEX)) return;
    base = sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t);
    if (header->msgh_size < base || header->msgh_size > available) return;
    body = (mach_msg_body_t *)(header + 1);
    count = body->msgh_descriptor_count;
    /* Kernel copyin normalized every descriptor to the kernel union stride. */
    if (count > (header->msgh_size - base) / sizeof(mach_msg_descriptor_t)) return;
    proc_name(proc_selfpid(), name, sizeof(name));
    if (strcmp(name, "lsd")) return;
    used = ios7lab_xpc_descriptor_lines;
    if (used >= 8 || !OSCompareAndSwap(used, used + 1,
        &ios7lab_xpc_descriptor_lines)) return;
    inspected = count < 4 ? count : 4;
    descriptors = (mach_msg_descriptor_t *)(body + 1);
    for (i = 0; i < inspected; ++i) {
        types[i] = descriptors[i].type.type;
        switch (types[i]) {
        case MACH_MSG_PORT_DESCRIPTOR:
            dispositions[i] = descriptors[i].port.disposition;
            break;
        case MACH_MSG_OOL_DESCRIPTOR:
        case MACH_MSG_OOL_VOLATILE_DESCRIPTOR:
            scalars[i] = descriptors[i].out_of_line.size;
            break;
        case MACH_MSG_OOL_PORTS_DESCRIPTOR:
            dispositions[i] = descriptors[i].ool_ports.disposition;
            scalars[i] = descriptors[i].ool_ports.count;
            break;
        default:
            break;
        }
    }
    /* Only type/disposition and length/count: no port/object/address/payload. */
    printf("IOS7LAB XPCDESC RECEIVE-PRE-COPYOUT seq=%u pid=%d tid=%llu id=%d internal-size=%u count=%u inspected=%u d0=%u/%u/%u d1=%u/%u/%u d2=%u/%u/%u d3=%u/%u/%u\n",
        used + 1, proc_selfpid(), (unsigned long long)thread_tid(current_thread()),
        header->msgh_id, header->msgh_size, count, inspected,
        types[0], dispositions[0], scalars[0], types[1], dispositions[1], scalars[1],
        types[2], dispositions[2], scalars[2], types[3], dispositions[3], scalars[3]);
}

static int ios7lab_ls_trace(mach_msg_header_t *header)
{
    static unsigned int count;
    int enabled = 0;
    char name[32] = {0};
    if (!header || (header->msgh_id != 14007 && header->msgh_id != 14107) || count >= 32)
        return 0;
    if (!PE_parse_boot_argn("ios7lab_vmabi", &enabled, sizeof(enabled)) || enabled != 1)
        return 0;
    proc_name(proc_selfpid(), name, sizeof(name));
    if (strcmp(name, "SpringBoard") && strcmp(name, "lsd") && strcmp(name, "installd"))
        return 0;
    ++count;
    return 1;
}
static int ios7lab_checkin_trace(mach_msg_header_t *header)
{
    static unsigned int count;
    int enabled = 0;
    if (!header || (header->msgh_id != 6003000 && header->msgh_id != 6003001) || count >= 64)
        return 0;
    if (!PE_parse_boot_argn("ios7lab_syscalls", &enabled, sizeof(enabled)) || enabled != 1)
        return 0;
    ++count;
    return 1;
}
#endif


#ifndef offsetof
#define offsetof(type, member)  ((size_t)(&((type *)0)->member))
#endif /* offsetof */

/*
 * Forward declarations - kernel internal routines
 */

mach_msg_return_t mach_msg_send(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		send_size,
	mach_msg_timeout_t	send_timeout,
	mach_port_name_t	notify);

mach_msg_return_t mach_msg_receive(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		rcv_size,
	mach_port_name_t	rcv_name,
	mach_msg_timeout_t	rcv_timeout,
	void 			(*continuation)(mach_msg_return_t),
	mach_msg_size_t		slist_size);


mach_msg_return_t msg_receive_error(
	ipc_kmsg_t		kmsg,
	mach_vm_address_t	msg_addr,
	mach_msg_option_t	option,
	mach_port_seqno_t	seqno,
	ipc_space_t		space);

security_token_t KERNEL_SECURITY_TOKEN = KERNEL_SECURITY_TOKEN_VALUE;
audit_token_t KERNEL_AUDIT_TOKEN = KERNEL_AUDIT_TOKEN_VALUE;

mach_msg_format_0_trailer_t trailer_template = {
	/* mach_msg_trailer_type_t */ MACH_MSG_TRAILER_FORMAT_0,
	/* mach_msg_trailer_size_t */ MACH_MSG_TRAILER_MINIMUM_SIZE,
        /* mach_port_seqno_t */       0,
	/* security_token_t */        KERNEL_SECURITY_TOKEN_VALUE
};

/*
 *	Routine:	mach_msg_send
 *	Purpose:
 *		Send a message.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		MACH_MSG_SUCCESS	Sent the message.
 *		MACH_SEND_MSG_TOO_SMALL	Message smaller than a header.
 *		MACH_SEND_NO_BUFFER	Couldn't allocate buffer.
 *		MACH_SEND_INVALID_DATA	Couldn't copy message data.
 *		MACH_SEND_INVALID_HEADER
 *			Illegal value in the message header bits.
 *		MACH_SEND_INVALID_DEST	The space is dead.
 *		MACH_SEND_INVALID_NOTIFY	Bad notify port.
 *		MACH_SEND_INVALID_DEST	Can't copyin destination port.
 *		MACH_SEND_INVALID_REPLY	Can't copyin reply port.
 *		MACH_SEND_TIMED_OUT	Timeout expired without delivery.
 *		MACH_SEND_INTERRUPTED	Delivery interrupted.
 */

/*
 *	Routine:	mach_msg_destroy_from_kernel
 *	Purpose:
 *		Destroy a message (usually an error reply) that was
 *		built in kernel memory by a MIG server routine.
 *		Cleans up port rights and out-of-line memory described
 *		by the message's descriptors.
 */
void
mach_msg_destroy_from_kernel(
	mach_msg_header_t       *msg)
{
	ipc_kmsg_t              kmsg;

	if (!(msg->msgh_bits & MACH_MSGH_BITS_COMPLEX))
		return;

	kmsg = ipc_kmsg_alloc(msg->msgh_size);
	if (kmsg == IKM_NULL)
		return;

	memcpy((void *) kmsg->ikm_header, (const void *) msg, msg->msgh_size);

	ipc_kmsg_destroy(kmsg);
}

mach_msg_return_t
mach_msg_send(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		send_size,
	mach_msg_timeout_t	send_timeout,
	__unused mach_port_name_t	notify)
{
	ipc_space_t space = current_space();
	vm_map_t map = current_map();
	ipc_kmsg_t kmsg;
	mach_msg_return_t mr;
	mach_msg_size_t	msg_and_trailer_size;
	mach_msg_max_trailer_t	*trailer;

	if ((send_size < sizeof(mach_msg_header_t)) || (send_size & 3))
		return MACH_SEND_MSG_TOO_SMALL;

	if (send_size > MACH_MSG_SIZE_MAX - MAX_TRAILER_SIZE)
		return MACH_SEND_TOO_LARGE;
	
	msg_and_trailer_size = send_size + MAX_TRAILER_SIZE;

	kmsg = ipc_kmsg_alloc(msg_and_trailer_size);

	if (kmsg == IKM_NULL)
		return MACH_SEND_NO_BUFFER;

	(void) memcpy((void *) kmsg->ikm_header, (const void *) msg, send_size);

	kmsg->ikm_header->msgh_size = send_size;

	/* 
	 * reserve for the trailer the largest space (MAX_TRAILER_SIZE)
	 * However, the internal size field of the trailer (msgh_trailer_size)
	 * is initialized to the minimum (sizeof(mach_msg_trailer_t)), to optimize
	 * the cases where no implicit data is requested.
	 */
	trailer = (mach_msg_max_trailer_t *) ((vm_offset_t)kmsg->ikm_header + send_size);
	trailer->msgh_sender = current_thread()->task->sec_token;
	trailer->msgh_audit = current_thread()->task->audit_token;
	trailer->msgh_trailer_type = MACH_MSG_TRAILER_FORMAT_0;
	trailer->msgh_trailer_size = MACH_MSG_TRAILER_MINIMUM_SIZE;
	
	mr = ipc_kmsg_copyin(kmsg, space, map, option & MACH_SEND_NOTIFY);
	if (mr != MACH_MSG_SUCCESS) {
		ipc_kmsg_free(kmsg);
		return mr;
	}

	mr = ipc_kmsg_send(kmsg, option & MACH_SEND_TIMEOUT, send_timeout);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
	{ int j1p=proc_selfpid(); if (j1p>=17)
	    kprintf("J1MSEND pid=%d id=%d rp=%x lp=%x rc=%x\n", j1p,
	        kmsg->ikm_header ? kmsg->ikm_header->msgh_id : -1,
	        kmsg->ikm_header ? (unsigned)kmsg->ikm_header->msgh_remote_port : 0,
	        kmsg->ikm_header ? (unsigned)kmsg->ikm_header->msgh_local_port : 0,
	        (unsigned)mr); }
#endif
	if (mr != MACH_MSG_SUCCESS) {
	    mr |= ipc_kmsg_copyout_pseudo(kmsg, space, map, MACH_MSG_BODY_NULL);
	    (void) memcpy((void *) msg, (const void *) kmsg->ikm_header, 
			  kmsg->ikm_header->msgh_size);
	    ipc_kmsg_free(kmsg);
	}

	return mr;
}

/*
 *	Routine:	mach_msg_receive_results
 *	Purpose:
 *		Receive a message.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		MACH_MSG_SUCCESS	Received a message.
 *		MACH_RCV_INVALID_NAME	The name doesn't denote a right,
 *			or the denoted right is not receive or port set.
 *		MACH_RCV_IN_SET		Receive right is a member of a set.
 *		MACH_RCV_TOO_LARGE	Message wouldn't fit into buffer.
 *		MACH_RCV_TIMED_OUT	Timeout expired without a message.
 *		MACH_RCV_INTERRUPTED	Reception interrupted.
 *		MACH_RCV_PORT_DIED	Port/set died while receiving.
 *		MACH_RCV_PORT_CHANGED	Port moved into set while receiving.
 *		MACH_RCV_INVALID_DATA	Couldn't copy to user buffer.
 *		MACH_RCV_INVALID_NOTIFY	Bad notify port.
 *		MACH_RCV_HEADER_ERROR
 */

mach_msg_return_t
mach_msg_receive_results(void)
{
	thread_t          self = current_thread();
	ipc_space_t       space = current_space();
	vm_map_t          map = current_map();

	ipc_object_t      object = self->ith_object;
	mach_msg_return_t mr = self->ith_state;
	mach_vm_address_t msg_addr = self->ith_msg_addr;
	mach_msg_option_t option = self->ith_option;
	ipc_kmsg_t        kmsg = self->ith_kmsg;
	mach_port_seqno_t seqno = self->ith_seqno;
	mach_msg_trailer_size_t trailer_size;
#if BOARD_CONFIG_QSD8250_LEO
    uint32_t leo_rpc060_receive_seq=leo_rpc060_receive_begin(self,mr,mr==MACH_MSG_SUCCESS?kmsg->ikm_header:0);
#endif
#if BOARD_CONFIG_ARMPBA8
    int lab_receive = mr == MACH_MSG_SUCCESS && ios7lab_checkin_trace(kmsg->ikm_header);
    int lab_message_id = lab_receive ? kmsg->ikm_header->msgh_id : 0;
    int lab_ls_receive = mr == MACH_MSG_SUCCESS && ios7lab_ls_trace(kmsg->ikm_header);
    int lab_ls_receive_id = lab_ls_receive ? kmsg->ikm_header->msgh_id : 0;
    mach_msg_size_t lab_ls_internal_size = lab_ls_receive ? kmsg->ikm_header->msgh_size : 0;
    int lab_boundary_receive = mr == MACH_MSG_SUCCESS ? ios7lab_boundary_trace(kmsg->ikm_header) : 0;
    int lab_boundary_receive_id = lab_boundary_receive ? kmsg->ikm_header->msgh_id : 0;
    mach_msg_size_t lab_boundary_internal_size = lab_boundary_receive ? kmsg->ikm_header->msgh_size : 0;
    mach_msg_bits_t lab_boundary_receive_bits = lab_boundary_receive ? kmsg->ikm_header->msgh_bits : 0;
    if (lab_boundary_receive)
        ios7lab_boundary_log(lab_boundary_receive, "RECEIVE-PRE-COPYOUT", lab_boundary_receive_id,
            lab_boundary_internal_size, 0, lab_boundary_receive_bits, option, mr);
    if (lab_boundary_receive == 1)
        ios7lab_xpc_descriptor_log(kmsg);
    if (lab_ls_receive) {
        unsigned int descriptors = 0;
        if ((kmsg->ikm_header->msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
            kmsg->ikm_header->msgh_size >= sizeof(mach_msg_header_t) + sizeof(mach_msg_body_t))
            descriptors = ((mach_msg_body_t *)(kmsg->ikm_header + 1))->msgh_descriptor_count;
        printf("IOS7LAB LSIPC RECEIVE pid=%d id=%d size=%u bits=%x descriptors=%u option=%x\n",
            proc_selfpid(), lab_ls_receive_id, kmsg->ikm_header->msgh_size,
            kmsg->ikm_header->msgh_bits, descriptors, option);
    }
    if (lab_receive)
        printf("IOS7LAB checkin RECEIVE pid=%d id=%d size=%u option=%x\n",
               proc_selfpid(), lab_message_id, kmsg->ikm_header->msgh_size, option);
#endif

	io_release(object);

	if (mr != MACH_MSG_SUCCESS) {

	  if (mr == MACH_RCV_TOO_LARGE ) {
	    if (option & MACH_RCV_LARGE) {
	      /*
	       * We need to inform the user-level code that it needs more
	       * space.  The value for how much space was returned in the
	       * msize save area instead of the message (which was left on
	       * the queue).
	       */
	      if (copyout((char *) &self->ith_msize,
			  msg_addr + offsetof(mach_msg_header_t, msgh_size),
			  sizeof(mach_msg_size_t)))
		mr = MACH_RCV_INVALID_DATA;
	      goto out;
	    }
		  
	    if (msg_receive_error(kmsg, msg_addr, option, seqno, space)
		== MACH_RCV_INVALID_DATA)
	      mr = MACH_RCV_INVALID_DATA;
	  }
	  goto out;
	}

	trailer_size = ipc_kmsg_add_trailer(kmsg, space, option, self, seqno, FALSE, 
			kmsg->ikm_header->msgh_remote_port->ip_context);
	/*
	 * If MACH_RCV_OVERWRITE was specified, try to get the scatter
	 * list and verify it against the contents of the message.  If
	 * there is any problem with it, we will continue without it as
	 * normal.
	 */
	if (option & MACH_RCV_OVERWRITE) {
		mach_msg_size_t slist_size = self->ith_scatter_list_size;
		mach_msg_body_t *slist;

		slist = ipc_kmsg_get_scatter(msg_addr, slist_size, kmsg);
		mr = ipc_kmsg_copyout(kmsg, space, map, slist);
		ipc_kmsg_free_scatter(slist, slist_size);
	} else {
		mr = ipc_kmsg_copyout(kmsg, space, map, MACH_MSG_BODY_NULL);
	}

#if BOARD_CONFIG_ARMPBA8
    /* Copyout may contract internal OOL descriptors. kmsg is still live here. */
    if (lab_ls_receive)
        printf("IOS7LAB LSIPC COPYOUT pid=%d tid=%llu id=%d internal-size=%u converted-user-size=%u result=%x\n",
            proc_selfpid(), (unsigned long long)thread_tid(self), lab_ls_receive_id,
            lab_ls_internal_size, mr == MACH_MSG_SUCCESS ? kmsg->ikm_header->msgh_size : 0, mr);
    if (lab_boundary_receive)
        ios7lab_boundary_log(lab_boundary_receive, "COPYOUT", lab_boundary_receive_id,
            lab_boundary_internal_size, mr == MACH_MSG_SUCCESS ? kmsg->ikm_header->msgh_size : 0,
            lab_boundary_receive_bits, option, mr);
#endif

	if (mr != MACH_MSG_SUCCESS) {
		if ((mr &~ MACH_MSG_MASK) == MACH_RCV_BODY_ERROR) {
			if (ipc_kmsg_put(msg_addr, kmsg, kmsg->ikm_header->msgh_size +
			   trailer_size) == MACH_RCV_INVALID_DATA)
				mr = MACH_RCV_INVALID_DATA;
		} 
		else {
			if (msg_receive_error(kmsg, msg_addr, option, seqno, space) 
						== MACH_RCV_INVALID_DATA)
				mr = MACH_RCV_INVALID_DATA;
		}
		goto out;
	}
	mr = ipc_kmsg_put(msg_addr,
			  kmsg,
			  kmsg->ikm_header->msgh_size + 
			  trailer_size);
 out:
#if BOARD_CONFIG_QSD8250_LEO
    leo_rpc060_note(leo_rpc060_receive_seq,LEO_RPC060_DONE,(uint32_t)mr,0);
#endif
#if BOARD_CONFIG_ARMPBA8
    if (lab_receive)
        printf("IOS7LAB checkin RECEIVE-RESULT pid=%d id=%d result=%x\n", proc_selfpid(), lab_message_id, mr);
    if (lab_ls_receive)
        printf("IOS7LAB LSIPC RECEIVE-RESULT pid=%d id=%d result=%x\n", proc_selfpid(), lab_ls_receive_id, mr);
    if (lab_boundary_receive)
        ios7lab_boundary_log(lab_boundary_receive, "RECEIVE-RESULT", lab_boundary_receive_id,
            lab_boundary_internal_size, 0, lab_boundary_receive_bits, option, mr);
#endif
	return mr;
}

mach_msg_return_t
mach_msg_receive(
	mach_msg_header_t	*msg,
	mach_msg_option_t	option,
	mach_msg_size_t		rcv_size,
	mach_port_name_t	rcv_name,
	mach_msg_timeout_t	rcv_timeout,
	void			(*continuation)(mach_msg_return_t),
	mach_msg_size_t		slist_size)
{
	thread_t self = current_thread();
	ipc_space_t space = current_space();
	ipc_object_t object;
	ipc_mqueue_t mqueue;
	mach_msg_return_t mr;

	mr = ipc_mqueue_copyin(space, rcv_name, &mqueue, &object);
 	if (mr != MACH_MSG_SUCCESS) {
		return mr;
	}
	/* hold ref for object */

	self->ith_msg_addr = CAST_DOWN(mach_vm_address_t, msg);
	self->ith_object = object;
	self->ith_msize = rcv_size;
	self->ith_option = option;
	self->ith_scatter_list_size = slist_size;
	self->ith_continuation = continuation;

	ipc_mqueue_receive(mqueue, option, rcv_size, rcv_timeout, THREAD_ABORTSAFE);
	if ((option & MACH_RCV_TIMEOUT) && rcv_timeout == 0)
		thread_poll_yield(self);
	return mach_msg_receive_results();
}

void
mach_msg_receive_continue(void)
{
	thread_t self = current_thread();

	(*self->ith_continuation)(mach_msg_receive_results());
}


/*
 *	Routine:	mach_msg_overwrite_trap [mach trap]
 *	Purpose:
 *		Possibly send a message; possibly receive a message.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		All of mach_msg_send and mach_msg_receive error codes.
 */

mach_msg_return_t
mach_msg_overwrite_trap(
	struct mach_msg_overwrite_trap_args *args)
{
  	mach_vm_address_t	msg_addr = args->msg;
	mach_msg_option_t	option = args->option;
	mach_msg_size_t		send_size = args->send_size;
	mach_msg_size_t		rcv_size = args->rcv_size;
	mach_port_name_t	rcv_name = args->rcv_name;
	mach_msg_timeout_t	msg_timeout = args->timeout;
	__unused mach_port_name_t notify = args->notify;
	mach_vm_address_t	rcv_msg_addr = args->rcv_msg;
        mach_msg_size_t		scatter_list_size = 0; /* NOT INITIALIZED - but not used in pactice */
	__unused mach_port_seqno_t temp_seqno = 0;
    
	mach_msg_return_t  mr = MACH_MSG_SUCCESS;
	vm_map_t map = current_map();
#if BOARD_CONFIG_QSD8250_LEO
    uint32_t leo_rpc060_seq=leo_rpc060_begin(args);
#endif
#if BOARD_CONFIG_ARMPBA8
    int lab_ls_send = 0, lab_ls_send_id = 0;
    int lab_boundary_send = 0, lab_boundary_send_id = 0;
    mach_msg_size_t lab_boundary_send_size = 0;
    mach_msg_bits_t lab_boundary_send_bits = 0;
#endif
	
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
	static unsigned int j1msgLogs;
	static unsigned int j1msgOol;
	{
		int j1cpid = proc_selfpid();
		if ((j1cpid >= 17) && j1msgLogs < 6000 && !(option & MACH_SEND_MSG)) {
			j1msgLogs++;
			kprintf("J1MSGR pid=%d opt=%x rname=%x tmo=%d\n", j1cpid, option, rcv_name, msg_timeout);
		}
	}
#endif
	if (option & MACH_SEND_MSG) {
		ipc_space_t space = current_space();
		ipc_kmsg_t kmsg;

		mr = ipc_kmsg_get(msg_addr, send_size, &kmsg);
#if BOARD_CONFIG_QSD8250_LEO
        leo_rpc060_note(leo_rpc060_seq,mr==MACH_MSG_SUCCESS?LEO_RPC060_HEADER:LEO_RPC060_DONE,(uint32_t)mr,mr==MACH_MSG_SUCCESS?kmsg->ikm_header:0);
#endif
		if (mr != MACH_MSG_SUCCESS) {
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
			int j1p = proc_selfpid();
			if (j1p >= 17) {
				mach_msg_header_t j1uh;
				int j1id = -1; unsigned int j1rp = 0, j1lp = 0;
				if (copyin(msg_addr, (char *)&j1uh, sizeof j1uh) == 0) {
					j1id = j1uh.msgh_id;
					j1rp = (unsigned)j1uh.msgh_remote_port;
					j1lp = (unsigned)j1uh.msgh_local_port;
				}
				kprintf("J1GETFAIL pid=%d id=%d rp=%x lp=%x rc=%x\n",
					j1p, j1id, j1rp, j1lp, (unsigned)mr);
			}
#endif
			return mr;
		}
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
		{
			int j1cpid = proc_selfpid();
			if ((j1cpid >= 17) && j1msgLogs < 6000) {
				char j1tmp[57];
				unsigned int j1n = kmsg->ikm_header->msgh_size, j1i;
				j1msgLogs++;
				if (j1n > 56) j1n = 56;
				bcopy((char *)kmsg->ikm_header + 24, j1tmp, j1n);
				for (j1i = 0; j1i < j1n; j1i++)
					if (j1tmp[j1i] < 0x20 || j1tmp[j1i] > 0x7e) j1tmp[j1i] = 0x2e;
				j1tmp[j1n] = 0;
				kprintf("J1MSGS pid=%d id=%d sz=%u rp=%x lp=%x b=%s\n", j1cpid,
					kmsg->ikm_header->msgh_id, kmsg->ikm_header->msgh_size,
					(unsigned)kmsg->ikm_header->msgh_remote_port,
					(unsigned)kmsg->ikm_header->msgh_local_port, j1tmp);
			}
			if ((j1cpid >= 17) && j1msgOol < 12000 &&
			    (kmsg->ikm_header->msgh_bits & 0x80000000U)) {
				unsigned int j1nd = *(unsigned int *)((char *)kmsg->ikm_header + 24);
				if (j1nd > 0 && j1nd < 8) {
					unsigned int *j1d = (unsigned int *)((char *)kmsg->ikm_header + 28);
					unsigned int j1oa = j1d[0], j1os = j1d[2];
					char j1o[401];
					unsigned int j1i2;
					if (j1os > 400) j1os = 400;
					if (j1oa >= 0x1000 && j1oa < 0xc0000000 && j1os > 0 &&
					    copyin(j1oa, j1o, j1os) == 0) {
						for (j1i2 = 0; j1i2 < j1os; j1i2++)
							if (j1o[j1i2] < 0x20 || j1o[j1i2] > 0x7e) j1o[j1i2] = 0x2e;
						j1o[j1os] = 0;
						j1msgOol++;
						kprintf("J1MSGO pid=%d id=%d ool=%x/%u data=%s\n",
							j1cpid, kmsg->ikm_header->msgh_id, j1oa, j1os, j1o);
					}
				}
			}
		}
#endif

#if BOARD_CONFIG_ARMPBA8
        int lab_send = ios7lab_checkin_trace(kmsg->ikm_header);
        int lab_send_id = lab_send ? kmsg->ikm_header->msgh_id : 0;
        lab_ls_send = ios7lab_ls_trace(kmsg->ikm_header);
        lab_ls_send_id = lab_ls_send ? kmsg->ikm_header->msgh_id : 0;
        lab_boundary_send = ios7lab_boundary_trace(kmsg->ikm_header);
        if (lab_boundary_send) {
            lab_boundary_send_id = kmsg->ikm_header->msgh_id;
            lab_boundary_send_size = kmsg->ikm_header->msgh_size;
            lab_boundary_send_bits = kmsg->ikm_header->msgh_bits;
            /* ipc_kmsg_get copied the normal sender header; copyin expands next. */
            ios7lab_boundary_log(lab_boundary_send, "SEND-PRE-COPYIN", lab_boundary_send_id,
                lab_boundary_send_size, 0, lab_boundary_send_bits, option, mr);
        }
        if (lab_ls_send)
            printf("IOS7LAB LSIPC SEND pid=%d id=%d size=%u destination-name=%x bits=%x option=%x receive-limit=%u\n",
                proc_selfpid(), lab_ls_send_id, kmsg->ikm_header->msgh_size,
                (unsigned)kmsg->ikm_header->msgh_remote_port, kmsg->ikm_header->msgh_bits,
                option, rcv_size);
        if (lab_send)
            printf("IOS7LAB checkin SEND pid=%d id=%d size=%u destination-name=%x bits=%x option=%x\n",
                   proc_selfpid(), lab_send_id, kmsg->ikm_header->msgh_size,
                   (unsigned)kmsg->ikm_header->msgh_remote_port, kmsg->ikm_header->msgh_bits, option);
#endif
		mr = ipc_kmsg_copyin(kmsg, space, map, option & MACH_SEND_NOTIFY);
#if BOARD_CONFIG_QSD8250_LEO
        leo_rpc060_note(leo_rpc060_seq,mr==MACH_MSG_SUCCESS?LEO_RPC060_COPYIN:LEO_RPC060_DONE,(uint32_t)mr,0);
#endif
		if (mr != MACH_MSG_SUCCESS) {
#if BOARD_CONFIG_ARMPBA8
            if (lab_send) printf("IOS7LAB checkin COPYIN-ERROR pid=%d id=%d result=%x\n", proc_selfpid(), lab_send_id, mr);
            if (lab_ls_send) printf("IOS7LAB LSIPC COPYIN-ERROR pid=%d id=%d result=%x\n", proc_selfpid(), lab_ls_send_id, mr);
            if (lab_boundary_send)
                ios7lab_boundary_log(lab_boundary_send, "COPYIN-ERROR", lab_boundary_send_id,
                    lab_boundary_send_size, 0, lab_boundary_send_bits, option, mr);
#endif
			ipc_kmsg_free(kmsg);
			return mr;
		}

#if BOARD_CONFIG_ARMPBA8
        if (lab_send) {
            ipc_port_t destination = (ipc_port_t)kmsg->ikm_header->msgh_remote_port;
            void *receiver_task = 0;
            mach_port_name_t receiver_name = 0;
            if (IP_VALID(destination)) {
                ip_lock(destination);
                if (ip_active(destination) && destination->ip_receiver_name) {
                    receiver_name = destination->ip_receiver_name;
                    receiver_task = destination->ip_receiver->is_task;
                }
                ip_unlock(destination);
            }
            printf("IOS7LAB checkin ROUTE id=%d receiver-task=%p receiver-name=%x\n",
                   lab_send_id, receiver_task, receiver_name);
        }
#endif
		mr = ipc_kmsg_send(kmsg, option & MACH_SEND_TIMEOUT, msg_timeout);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
		if (mr != MACH_MSG_SUCCESS) {
			int j1p = proc_selfpid();
			if (j1p >= 17)
				kprintf("J1SENDFAIL pid=%d id=%d rc=%x\n", j1p,
					kmsg->ikm_header ? kmsg->ikm_header->msgh_id : -1,
					(unsigned)mr);
		}
#endif
#if BOARD_CONFIG_QSD8250_LEO
        leo_rpc060_note(leo_rpc060_seq,mr==MACH_MSG_SUCCESS?LEO_RPC060_SEND:LEO_RPC060_DONE,(uint32_t)mr,0);
#endif
#if BOARD_CONFIG_ARMPBA8
        if (lab_send) printf("IOS7LAB checkin SEND-RESULT pid=%d id=%d result=%x\n", proc_selfpid(), lab_send_id, mr);
        if (lab_ls_send) printf("IOS7LAB LSIPC SEND-RESULT pid=%d id=%d result=%x\n", proc_selfpid(), lab_ls_send_id, mr);
        if (lab_boundary_send)
            ios7lab_boundary_log(lab_boundary_send, "SEND-RESULT", lab_boundary_send_id,
                lab_boundary_send_size, 0, lab_boundary_send_bits, option, mr);
#endif

		if (mr != MACH_MSG_SUCCESS) {
			mr |= ipc_kmsg_copyout_pseudo(kmsg, space, map, MACH_MSG_BODY_NULL);
			(void) ipc_kmsg_put(msg_addr, kmsg, kmsg->ikm_header->msgh_size);
#if BOARD_CONFIG_QSD8250_LEO
            leo_rpc060_note(leo_rpc060_seq,LEO_RPC060_DONE,(uint32_t)mr,0);
#endif
			return mr;
		}
	}

	if (option & MACH_RCV_MSG) {
#if BOARD_CONFIG_ARMPBA8
        if (lab_ls_send)
            printf("IOS7LAB LSIPC RECEIVE-WAIT pid=%d request-id=%d receive-name=%x limit=%u option=%x\n",
                proc_selfpid(), lab_ls_send_id, rcv_name, rcv_size, option);
#endif
		thread_t self = current_thread();
		ipc_space_t space = current_space();
		ipc_object_t object;
		ipc_mqueue_t mqueue;

		mr = ipc_mqueue_copyin(space, rcv_name, &mqueue, &object);
		if (mr != MACH_MSG_SUCCESS) {
#if BOARD_CONFIG_QSD8250_LEO
            leo_rpc060_note(leo_rpc060_seq,LEO_RPC060_DONE,(uint32_t)mr,0);
#endif
			return mr;
		}
		/* hold ref for object */

		/*
		 * 1. MACH_RCV_OVERWRITE is on, and rcv_msg is our scatter list
		 *    and receive buffer
		 * 2. MACH_RCV_OVERWRITE is off, and rcv_msg might be the
		 *    alternate receive buffer (separate send and receive buffers).
		 */
		if (option & MACH_RCV_OVERWRITE) 
			self->ith_msg_addr = rcv_msg_addr;
		else if (rcv_msg_addr != (mach_vm_address_t)0)
			self->ith_msg_addr = rcv_msg_addr;
		else
			self->ith_msg_addr = msg_addr;
		self->ith_object = object;
		self->ith_msize = rcv_size;
		self->ith_option = option;
		self->ith_scatter_list_size = scatter_list_size;
		self->ith_receiver_name = MACH_PORT_NULL;
		self->ith_continuation = thread_syscall_return;

#if BOARD_CONFIG_QSD8250_LEO
        leo_rpc060_note(leo_rpc060_seq,LEO_RPC060_WAIT,MACH_MSG_SUCCESS,0);
#endif
		ipc_mqueue_receive(mqueue, option, rcv_size, msg_timeout, THREAD_ABORTSAFE);
		if ((option & MACH_RCV_TIMEOUT) && msg_timeout == 0)
			thread_poll_yield(self);
		return mach_msg_receive_results();
	}

#if BOARD_CONFIG_QSD8250_LEO
    leo_rpc060_note(leo_rpc060_seq,LEO_RPC060_DONE,MACH_MSG_SUCCESS,0);
#endif
	return MACH_MSG_SUCCESS;
}

/*
 *	Routine:	mach_msg_trap [mach trap]
 *	Purpose:
 *		Possibly send a message; possibly receive a message.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		All of mach_msg_send and mach_msg_receive error codes.
 */

mach_msg_return_t
mach_msg_trap(
	struct mach_msg_overwrite_trap_args *args)
{
	kern_return_t kr;
#if BOARD_CONFIG_EXYNOS4210_I9100
	mach_port_name_t rport = MACH_PORT_NULL;
	mach_port_name_t lport = MACH_PORT_NULL;
	mach_msg_id_t mid = -1;
	char pn[17]; pn[0] = 0;
	int pid = proc_selfpid();
	static int mm_gate = -1;
	if (mm_gate < 0) {
		int v = 0;
		PE_parse_boot_argn("i9100mmtrap", &v, sizeof(v));
		mm_gate = v;
	}
	if (mm_gate && pid >= 1 && pid <= 4 && args->msg) {
		mach_msg_header_t h;
		if (copyin((user_addr_t)args->msg, (char *)&h,
			   sizeof h) == 0) {
			rport = h.msgh_remote_port;
			lport = h.msgh_local_port;
			mid = h.msgh_id;
		}
		proc_name(pid, pn, sizeof pn);
	}
#endif
	args->rcv_msg = (mach_vm_address_t)0;

 	kr = mach_msg_overwrite_trap(args);
#if BOARD_CONFIG_EXYNOS4210_I9100
	if (mm_gate && pid >= 1 && pid <= 4) {
		kprintf("MMTRAP pid=%d(%s) opt=%x snd=%u rcv=%u "
			"rport=%x lport=%x rname=%x id=%d kr=%x\n",
			pid, pn, (unsigned)args->option,
			(unsigned)args->send_size, (unsigned)args->rcv_size,
			(unsigned)rport, (unsigned)lport,
			(unsigned)args->rcv_name, mid, (unsigned)kr);
		if (rport == MACH_PORT_NULL) {
			arm_saved_state_t *us =
				(arm_saved_state_t *)current_thread()->machine.uss;
			kprintf("MMNULL pid=%d upc=%08x ulr=%08x sp=%08x\n",
				pid, (unsigned)us->pc, (unsigned)us->lr,
				(unsigned)us->sp);
		}
	}
#endif
	return kr;
}
 

/*
 *	Routine:	msg_receive_error	[internal]
 *	Purpose:
 *		Builds a minimal header/trailer and copies it to
 *		the user message buffer.  Invoked when in the case of a
 *		MACH_RCV_TOO_LARGE or MACH_RCV_BODY_ERROR error.
 *	Conditions:
 *		Nothing locked.
 *	Returns:
 *		MACH_MSG_SUCCESS	minimal header/trailer copied
 *		MACH_RCV_INVALID_DATA	copyout to user buffer failed
 */
	
mach_msg_return_t
msg_receive_error(
	ipc_kmsg_t		kmsg,
	mach_vm_address_t	msg_addr,
	mach_msg_option_t	option,
	mach_port_seqno_t	seqno,
	ipc_space_t		space)
{
	mach_vm_address_t	context;
	mach_msg_trailer_size_t trailer_size;
	mach_msg_max_trailer_t	*trailer;

	context = kmsg->ikm_header->msgh_remote_port->ip_context;

	/*
	 * Copy out the destination port in the message.
 	 * Destroy all other rights and memory in the message.
	 */
	ipc_kmsg_copyout_dest(kmsg, space);

	/*
	 * Build a minimal message with the requested trailer.
	 */
	trailer = (mach_msg_max_trailer_t *) 
			((vm_offset_t)kmsg->ikm_header +
			round_msg(sizeof(mach_msg_header_t)));
	kmsg->ikm_header->msgh_size = sizeof(mach_msg_header_t);
	bcopy(  (char *)&trailer_template, 
		(char *)trailer, 
		sizeof(trailer_template));

	trailer_size = ipc_kmsg_add_trailer(kmsg, space, 
			option, current_thread(), seqno,
			TRUE, context);

	/*
	 * Copy the message to user space
	 */
	if (ipc_kmsg_put(msg_addr, kmsg, kmsg->ikm_header->msgh_size +
			trailer_size) == MACH_RCV_INVALID_DATA)
		return(MACH_RCV_INVALID_DATA);
	else 
		return(MACH_MSG_SUCCESS);
}
