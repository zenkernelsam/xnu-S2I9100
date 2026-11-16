#ifndef I9100_SETUP_NATIVE_LAYOUT_H
#define I9100_SETUP_NATIVE_LAYOUT_H
#include <libkern/OSAtomic.h>

/* Platform preference for the original iOS6 Setup on the S2's native panel.
 * Change the launch input before it reaches UIKit, not UIKit flags/geometry.
 * Index: schema, matched, classic entries, changed, missing, already native,
 * other value, invalid workspace, duplicate keys. Default is disabled. */
volatile uint32_t i9100_setup_native_layout_diag[16]={1};
static void i9100_setup_native_count(unsigned field)
{
    volatile UInt32 *value=&i9100_setup_native_layout_diag[field];
    UInt32 old;
    do { old=*value; if(old==UINT32_MAX)return; }
    while(!OSCompareAndSwap(old,old+1,value));
}
static void i9100_setup_native_layout(struct image_params *imgp,
    const uint8_t *uuid,unsigned pointer_size)
{
    static const uint8_t original_uuid[16]={
        0x58,0x0d,0xfd,0x1e,0xdb,0xd5,0x3d,0x33,
        0x84,0x4b,0xca,0x66,0x35,0xc0,0xb3,0xcd};
    int enabled=0,abi=0;
    if(!imgp || !uuid || pointer_size!=4 ||
       !PE_parse_boot_argn("i9100setupnative",&enabled,4) || enabled!=1 ||
       !PE_parse_boot_argn("i9100hidabi",&abi,4) || abi!=6 ||
       memcmp(uuid,original_uuid,16) || !imgp->ip_ndp ||
       imgp->ip_ndp->ni_cnd.cn_namelen!=5 || !imgp->ip_ndp->ni_cnd.cn_nameptr ||
       memcmp(imgp->ip_ndp->ni_cnd.cn_nameptr,"Setup",5))return;
    i9100_setup_native_count(1);
    if(!imgp->ip_strings || !imgp->ip_endargv || !imgp->ip_endenvv || !imgp->ip_strendp ||
       imgp->ip_envc<0 || imgp->ip_envc>256 ||
       imgp->ip_endargv<imgp->ip_strings || imgp->ip_endenvv<imgp->ip_endargv ||
       imgp->ip_endenvv>imgp->ip_strendp){i9100_setup_native_count(7);return;}
    char *cursor=imgp->ip_endargv,*end=imgp->ip_endenvv,*entry=NULL;
    unsigned count=0;size_t entry_length=0;
    /* Validate the entire vector before editing any byte. Duplicate keys or
     * malformed input keep the original process environment unchanged. */
    for(int n=0;n<imgp->ip_envc;n++){
        if(cursor>=end){i9100_setup_native_count(7);return;}
        char *zero=cursor;
        while(zero<end && *zero)zero++;
        if(zero==end){i9100_setup_native_count(7);return;}
        size_t length=(size_t)(zero-cursor);
        if(length>=8 && !memcmp(cursor,"CLASSIC=",8)){
            count++;entry=cursor;entry_length=length;i9100_setup_native_count(2);
        }
        cursor=zero+1;
    }
    if(count>1){i9100_setup_native_count(8);return;}
    if(!count){i9100_setup_native_count(4);return;}
    if(entry_length==9 && entry[8]=='0'){i9100_setup_native_count(5);return;}
    if(entry_length!=9 || entry[8]!='1'){i9100_setup_native_count(6);return;}
    entry[8]='0';i9100_setup_native_count(3);
}
#endif
