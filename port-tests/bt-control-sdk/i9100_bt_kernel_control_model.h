/* SPDX-License-Identifier: BSD-2-Clause. BSD socket/proc facade, host only. */
#ifndef I9100_BT_KERNEL_CONTROL_MODEL_H
#define I9100_BT_KERNEL_CONTROL_MODEL_H
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
typedef int errno_t;
typedef void *kern_ctl_ref;
typedef void *kauth_cred_t;
typedef void *proc_t;
typedef struct i9100_model_mbuf *mbuf_t;
struct sockaddr_ctl {uint8_t sc_len,sc_family;uint16_t ss_sysaddr;uint32_t sc_id,sc_unit,sc_reserved[5];};
struct kern_ctl_reg {
 char ctl_name[96];uint32_t ctl_id,ctl_unit,ctl_flags,ctl_sendsize,ctl_recvsize;
 errno_t (*ctl_connect)(kern_ctl_ref,struct sockaddr_ctl *,void **);
 errno_t (*ctl_disconnect)(kern_ctl_ref,uint32_t,void *);
 errno_t (*ctl_send)(kern_ctl_ref,uint32_t,void *,mbuf_t,int);
 errno_t (*ctl_setopt)(kern_ctl_ref,uint32_t,void *,int,void *,size_t);
 errno_t (*ctl_getopt)(kern_ctl_ref,uint32_t,void *,int,void *,size_t *);
};
#define CTL_FLAG_REG_SOCK_STREAM 4U
extern "C" {
errno_t ctl_register(struct kern_ctl_reg *,kern_ctl_ref *);
errno_t ctl_deregister(kern_ctl_ref);
errno_t ctl_enqueuedata(kern_ctl_ref,uint32_t,void *,size_t,uint32_t);
void mbuf_freem(mbuf_t);
mbuf_t mbuf_next(mbuf_t);
size_t mbuf_len(mbuf_t);
errno_t mbuf_copydata(mbuf_t,size_t,size_t,void *);
int proc_selfpid(void);
uint64_t proc_selfuniqueid(void);
void proc_selfname(char *,int);
proc_t proc_self(void);
int proc_rele(proc_t);
void proc_getexecutableuuid(proc_t,unsigned char *,unsigned long);
kauth_cred_t kauth_cred_get(void);
unsigned kauth_cred_getuid(kauth_cred_t);
size_t strlcpy(char *,const char *,size_t);
}
#endif
