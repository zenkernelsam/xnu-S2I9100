#!/usr/bin/env python3
"""Actual ARM mapping lifecycle, PV promotion, wire ledgers and target-map walks."""
from pathlib import Path
from datetime import datetime
import os,re,subprocess,sys,argparse
if os.environ.get('I9100_CPU_CAPPED') != '1':
 os.execvpe('taskset', ['taskset', '-c', '0-13', 'nice', '-n10', 'ionice', '-c2', '-n7',
                       sys.executable, '-B', __file__, *sys.argv[1:]],
            dict(os.environ, I9100_CPU_CAPPED='1'))
root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=root/'osfmk/arm/pmap.c')
parser.add_argument('--refmod',action='store_true')
parser.add_argument('--old-refmod-control',action='store_true')
parser.add_argument('--old-unwire-control',action='store_true',
                    help='Require the old source to panic on a removed kernel mapping')
parser.add_argument('--reject-unwire',type=int,choices=range(1,8),
                    help='Require a specific malformed wiring operation to panic')
args=parser.parse_args()
if sum(bool(x) for x in (args.old_refmod_control, args.old_unwire_control,
                         args.reject_unwire)) > 1:
 parser.error('Use only one negative control per run')
s=args.source.read_text()
def function(name):
 m=re.search(r'(?m)^(?:static )?(?:inline )?(?:__attribute__ \(\(always_inline\)\) )?(?:unsigned int|unsigned|uint32_t|void|boolean_t|kern_return_t|pt_entry_t|vm_offset_t|pv_rooted_entry_t|pv_hashed_entry_t) '+name+r'\([^;]*?\)\s*\{',s)
 if not m:raise RuntimeError(name)
 depth=1
 for end in range(m.end(),len(s)):
  depth+=(s[end]=='{')-(s[end]=='}')
  if not depth:return s[m.start():end+1]
names=['pvhashidx','pv_hash_add','pmap_pvh_unlink','pv_hash_remove','pmap_pv_remove',
 'leo_boot_present','leo_boot_match','leo_boot_forget','leo_boot_pair_add','leo_boot_range_add',
 'leo_boot_bd_replace','leo_boot_seed_linear','leo_mapping_find','leo_physical_wire_refresh','leo_mapping_wire','leo_node_get','leo_mapping_add','leo_node_free','leo_mapping_remove',
 'leo_pte_build','leo_pte_publish','leo_enter','leo_change_wiring','leo_remove_range',
 'pmap_enter_options','pmap_change_wiring','pmap_remove_range','pmap_page_protect','pmap_extract','pmap_clear_refmod']
names[0:0]=[n for n in ['i9100_pte_refmod','i9100_refmod_preserve','i9100_refmod_snapshot'] if re.search(r'(?m)^static (?:unsigned|void) '+n+r'\(',s)]
if args.refmod or args.old_refmod_control:
 names += ['pmap_get_refmod','pmap_is_modified','pmap_is_referenced','pmap_clear_modify','pmap_clear_reference','pmap_disconnect']
structs=s[s.index('typedef struct pv_rooted_entry {    /* common prefix'):s.index('#define NPVHASH')]
# The implementation declarations are included after mock VM definitions.
d=root/'out'/('mapping-check-'+datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
d.mkdir()
(d/'source-pmap.c').write_text(s)
(d/'mapping-structs.inc').write_text(structs)
(d/'mapping-under-test.inc').write_text('\n'.join(function(n) for n in names))
(d/'start.S').write_text('.syntax unified\n.arm\n.global _start\n_start:\n cpsid if\n ldr sp,=0x60200000\n bl run\n1: b 1b\n')
subprocess.run(['arm-none-eabi-gcc','-mcpu=cortex-a9','-marm','-O1','-ffreestanding','-fno-builtin','-nostdlib','-DBOARD_CONFIG_EXYNOS4210_I9100=1',*(['-DRM_TEST=1'] if args.refmod or args.old_refmod_control else []),*(['-DRM_OLD_CONTROL=1'] if args.old_refmod_control else []),*(['-DUNWIRE_OLD_CONTROL=1'] if args.old_unwire_control else []),*(['-DUNWIRE_REJECT_CASE='+str(args.reject_unwire)] if args.reject_unwire else []),'-I'+str(d),'-Wl,-Ttext,0x60010000','-Wl,-e,_start',str(d/'start.S'),str(root/'port-tests/i9100_mapping_test.c'),'-o',str(d/'test.elf')],check=True)
r=subprocess.run(['qemu-system-arm','-M','vexpress-a9','-cpu','cortex-a9','-m','128M','-nographic','-monitor','none','-serial','none','-semihosting-config','enable=on,target=native','-kernel',str(d/'test.elf')],env=dict(os.environ,QEMU_AUDIO_DRV='none'),capture_output=True,text=True,timeout=20)
(d/'result.txt').write_text(r.stdout+r.stderr)
print(r.stdout+r.stderr,end='')
print('EVIDENCE', d)
negative=args.old_refmod_control or args.old_unwire_control or args.reject_unwire
raise SystemExit((0 if r.returncode==23 else 1) if negative else r.returncode)
