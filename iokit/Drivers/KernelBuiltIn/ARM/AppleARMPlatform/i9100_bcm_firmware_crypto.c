/* SPDX-License-Identifier: BSD-2-Clause */
#include "i9100_bcm_firmware.h"
#include <libkern/crypto/crypto_internal.h>
#include <libkern/crypto/sha2.h>
#include <string.h>
int i9100_bcm_fw_kernel_sha256(void *cookie,const void *bytes,uint32_t n,uint8_t digest[32])
{
 (void)cookie;
 if(!bytes||!digest||!n||n>I9100_BCM_MAX_RAM)return I9100_BCM_FW_ARGUMENT;
 crypto_functions_t provider=g_crypto_funcs;memset(digest,0,32);
 if(!provider||!provider->ccsha256_di||!provider->ccdigest_init_fn||!provider->ccdigest_update_fn)
  return I9100_BCM_FW_CRYPTO;
 const struct ccdigest_info *di=provider->ccsha256_di;
 if(di->output_size!=32U||di->state_size!=32U||di->block_size!=64U||
    !di->initial_state||!di->compress||!di->final)return I9100_BCM_FW_CRYPTO;
 SHA256_CTX ctx;memset(&ctx,0,sizeof(ctx));
 if(ccdigest_di_size(di)>sizeof(ctx))return I9100_BCM_FW_CRYPTO;
 /* Use the captured registered provider throughout; no SHA wrapper that
  * can panic on a missing provider or unexpectedly reload g_crypto_funcs. */
 provider->ccdigest_init_fn(di,ctx.ctx);
 provider->ccdigest_update_fn(di,ctx.ctx,n,bytes);
 di->final(di,ctx.ctx,digest);memset(&ctx,0,sizeof(ctx));return 0;
}
