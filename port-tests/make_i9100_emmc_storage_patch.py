#!/usr/bin/env python3
"""Prepare unapplied BSD real-node alias/root matching hooks, leaving the live mainline unchanged."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
from i9100_offline_build import REPO,cap_cpu
cap_cpu()
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write-unapplied-patch',action='store_true',required=True);p.parse_args()
updates={};sources={}
def read(name):
    raw=(REPO/name).read_bytes();sources[name]=hashlib.sha256(raw).hexdigest();return raw.decode()
def replace(text,before,after):
    if text.count(before)!=1:raise RuntimeError('Actual main source hook changed')
    return text.replace(before,after)
name='iokit/Kernel/Storage/IOMediaBSDClient.cpp';text=read(name)
anchor='#include <IOKit/storage/IOMediaBSDClient.h>'
text=replace(text,anchor,anchor+'\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n#include "i9100_emmc_alias.h"\n#endif')
anchor='    createNodes(media);'
text=replace(text,anchor,'''    bool i9100CreatedNodes = createNodes(media);
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    // Pin under BSD state before a concurrent minor removal can release it.
    uint64_t i9100AliasTicket = i9100CreatedNodes ? i9100_emmc_alias_ticket(media) : 0;
    if (i9100AliasTicket) media->retain();
#endif''')
anchor='    gIOMediaBSDClientGlobals.unlockState();\n\n    // Register this object'
text=replace(text,anchor,'''    gIOMediaBSDClientGlobals.unlockState();

#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (!i9100CreatedNodes && i9100_emmc_alias_required(media)) return false;
    if (i9100AliasTicket) {
        i9100_emmc_alias_commit(media, i9100AliasTicket);
        media->release();
    }
#endif

    // Register this object''')
anchor='    // Construct a name for the node.\n\n    snprintf(minorName, minorNameSize, "disk%d%s", (int) anchorID, slicePath);'
# This appears in insert/update; only the insert's node-allocation block
# contains the unique preamble and is eligible for alias publication.
start=text.index('UInt32 MinorTable::insert(');end=text.index('void MinorTable::remove(',start)
part=text[start:end]
addition='''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    uint64_t i9100NewAliasTicket = 0;
    if (i9100_emmc_alias_publish(media, bdevNode, cdevNode, &i9100NewAliasTicket) != 0)
    {
        devfs_remove(cdevNode);
        devfs_remove(bdevNode);
        i9100_emmc_alias_unpublish(media, i9100NewAliasTicket);
        IODelete(minorName, char, minorNameSize);
        return kInvalidMinorID;
    }
#endif

'''
part=replace(part,anchor,addition+anchor);text=text[:start]+part+text[end:]
anchor='    devfs_remove(_table[minorID].cdevNode);'
text=replace(text,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    uint64_t i9100RemovedAliasTicket = i9100_emmc_alias_ticket(_table[minorID].media);
    i9100_emmc_alias_invalidate(_table[minorID].media);
#endif
''' + anchor)
anchor='    devfs_remove(_table[minorID].bdevNode);'
text=replace(text,anchor,anchor+'\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n    i9100_emmc_alias_unpublish(_table[minorID].media, i9100RemovedAliasTicket);\n#endif')
anchor='    _table[minorID].isObsolete = true;'
text=replace(text,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    i9100_emmc_alias_invalidate(_table[minorID].media);
#endif
''' + anchor)
# Native formatting is unsupported. Do not reuse an orphan's nodes and let
# their old aliases silently refer to another lifetime of the same name.
anchor='    // Update the slot and retain the appropriate objects.'
text=replace(text,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (i9100_emmc_alias_required(media) || i9100_emmc_alias_required(_table[minorID].media))
        return kInvalidMinorID;
#endif

''' + anchor)
updates[name]=text
name='iokit/bsddev/IOKitBSDInit.cpp';text=read(name)
anchor='#include <IOKit/IOPlatformExpert.h>'
text=replace(text,anchor,anchor+'\n#if defined(BOARD_CONFIG_EXYNOS4210_I9100)\n#include <IOKit/storage/IOMedia.h>\n#include "i9100_emmc_alias.h"\n#endif')
anchor='    bool\t\tdebugInfoPrintedOnce = false;'
text=replace(text,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    bool i9100InternalRoot = false;
    IOMedia *i9100PinnedRoot = 0;
#endif
''' + anchor)
anchor='\t    matching = IOBSDNameMatching( look );'
replacement='''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
            if (strcmp(look, "i9100-system") == 0) {
                matching = IOService::resourceMatching("i9100-system-media");
                if (!matching) panic("i9100 root matching allocation failed");
                i9100InternalRoot = true;
            } else
#endif
''' + anchor
text=replace(text,anchor,replacement)
anchor='    if ( service && mediaProperty ) {'
text=replace(text,anchor,'''#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (i9100InternalRoot) {
        i9100PinnedRoot = i9100_emmc_alias_copy_root();
        if (!i9100PinnedRoot) panic("i9100 internal volume pair withdrawn before root selection");
        service = i9100PinnedRoot;
    }
#endif
''' + anchor)
anchor='iofrootx:'
text=replace(text,anchor,anchor+'''
#if defined(BOARD_CONFIG_EXYNOS4210_I9100)
    if (i9100PinnedRoot) i9100PinnedRoot->release();
#endif''')
updates[name]=text
patch=''
for name,text in updates.items():
    before=(REPO/name).read_text()
    patch+=''.join(difflib.unified_diff(before.splitlines(True),text.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
target=REPO/'port-tests/i9100_emmc_storage_integration.patch';meta=target.with_suffix('.json')
target.write_text(patch)
meta.write_text(json.dumps(dict(sources=sources,patch_sha256=hashlib.sha256(patch.encode()).hexdigest(),
    applied=False,hardware_enabled=False,paired_objects=['iokit/IOMediaBSDClient.cpo','iokit/IOKitBSDInit.cpo'],
    requirements='Explicit native0 DATA_RW+readonly-whole/pair publication and reviewed current profile; no live registration or device use'),indent=2)+'\n')
for name,expected in sources.items():
    if hashlib.sha256((REPO/name).read_bytes()).hexdigest()!=expected:raise RuntimeError('Live main source changed; patch stale')
print('PREPARED unapplied native-media BSD aliases/root matching; live files unchanged')
