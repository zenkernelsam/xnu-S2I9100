/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef I9100_EMMC_ALIAS_H
#define I9100_EMMC_ALIAS_H
#include <stdint.h>
#ifdef __cplusplus
class IOMedia;
bool i9100_emmc_alias_prepare();
bool i9100_emmc_alias_required(IOMedia *);
int i9100_emmc_alias_publish(IOMedia *,void *actual_bdev_node,void *actual_cdev_node,uint64_t *owned_creation_ticket);
uint64_t i9100_emmc_alias_ticket(IOMedia *);
void i9100_emmc_alias_commit(IOMedia *,uint64_t actual_node_creation_ticket);
IOMedia *i9100_emmc_alias_copy_root(); /* retained, only a current complete pair */
void i9100_emmc_alias_invalidate(IOMedia *); /* BEFORE removal/obsolescence */
void i9100_emmc_alias_unpublish(IOMedia *,uint64_t actual_node_creation_ticket);
/* Only the exact validated IOMedia from IOS6I9100EMMCStorage may publish
 * a fixed /dev alias. Hook runs AFTER the normal MinorTable nodes exist.
 * No new dev_t, major/minor, tty or successful fake block node is made.
 * Node removal uses original devfs_remove, which removes its own links.
 * Prepare the process-context lifetime gate before starting BSD matching.
 * Slots retain both media and the exact provider across detach/late commit.
 * Capture ticket and media retain under BSD state after createNodes; commit
 * runs only after MinorTable has assigned BSD name/major/minor, outside BSD;
 * root matching must not observe ready while the minor is incomplete.
 * Only a complete system+data pair publishes the root media resource. Root
 * selection copies a retained current pair member under the same gate.
 * Invalidate withdraws readiness BEFORE original nodes can disappear; it
 * keeps name ownership until both originals are gone. Then unpublish.
 * Publish may partially fail; keep role ownership until both original
 * nodes are removed, then unpublish. Duplicate/reused names cannot steal
 * another live node. Existing media without this exact provider are inert. */
#endif
#endif
