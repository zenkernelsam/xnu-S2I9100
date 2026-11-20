"""Compile real guarded/native DMA for eMMC models with only a host DSB substitution."""
import hashlib
from i9100_offline_build import PLATFORM,REPO
def prepare(e):
    source=PLATFORM/'i9100_dma_native.cpp';raw=source.read_bytes()
    token=b'__asm__ volatile("dsb sy" ::: "memory");'
    if raw.count(token)!=2:raise RuntimeError('Actual allocator DSB sites changed')
    copied=e.out/'i9100_dma_native.cpp';copied.write_bytes(raw.replace(token,b'OSMemoryBarrier(); /* explicit host DSB facade */'))
    e.result['host_source_override']=dict(original_sha256=hashlib.sha256(raw).hexdigest(),copy_sha256=hashlib.sha256(copied.read_bytes()).hexdigest(),substitution='two original allocator DSB sites, no allocation/MMC changes')
    companion=REPO/'port-tests/i9100_emmc_dma_facade.inc'
    e.result['dma_facade_sha256']=hashlib.sha256(companion.read_bytes()).hexdigest()
    return {'i9100_dma_native.cpp':copied}
