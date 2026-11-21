/* SPDX-License-Identifier: BSD-2-Clause
 * Execute exact pinned LGPL reference on host only, against independent
 * cells also used to populate the actual production decoder's fixtures. */
#include <gst/video/video-tile.h>
#include <stdio.h>
#include "i9100_mfc_tile_model.h"
int main(void)
{
 for(unsigned xt=2;xt<=32;xt+=2)for(unsigned yt=1;yt<=35;yt++){
  for(unsigned ty=0;ty<yt;ty++)for(unsigned tx=0;tx<xt;tx++){
   assert(gst_video_tile_get_index(GST_VIDEO_TILE_MODE_ZFLIPZ_2X2,tx,ty,xt,yt)==tile(tx,ty,xt,yt));
   assert(gst_video_tile_get_index(GST_VIDEO_TILE_MODE_LINEAR,tx,ty,xt,yt)==ty*xt+tx);
  }
 }
 puts("PASS exact pinned GStreamer1.20 tile function vs independent 2x2 cells; every even pitch2..32tiles, odd/even height1..35; reference-only host executable, hardware=false");return 0;
}
