"""Host-only CPU barrier substitution; original DMA allocation flow is linked."""
import hashlib
from i9100_offline_build import PLATFORM,REPO

def prepare(e):
    raw=(PLATFORM/'i9100_dma_native.cpp').read_bytes()
    token=b'__asm__ volatile("dsb sy" ::: "memory");'
    if raw.count(token)!=2:raise RuntimeError('Re-audit host-only DSB substitutions')
    copy=e.out/'i9100_dma_native.cpp'
    copy.write_bytes(raw.replace(token,b'OSMemoryBarrier(); /* host-only DSB facade */'))
    e.result['host_source_override']=dict(original_sha256=hashlib.sha256(raw).hexdigest(),copy_sha256=hashlib.sha256(copy.read_bytes()).hexdigest(),
      substitution='two DSB sites only; unmodified allocator control flow')
    e.result['mfc_dma_facade_sha256']=hashlib.sha256((REPO/'port-tests/i9100_mfc_dma_facade.inc').read_bytes()).hexdigest()
    return {'i9100_dma_native.cpp':copy}
