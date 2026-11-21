/* SPDX-License-Identifier: BSD-2-Clause
 * Independent cell-coordinate fixture, no production index expression. */
#ifndef I9100_MFC_TILE_MODEL_H
#define I9100_MFC_TILE_MODEL_H
static unsigned tile(unsigned tx,unsigned ty,unsigned xt,unsigned yt)
{
 if((yt&1U)&&ty==yt-1U)return ty*xt+tx;
 unsigned group=tx/2U,ox=tx%2U,oy=ty%2U;
 unsigned within=(group&1U)?(1U-oy)*2U+ox:oy*2U+ox;
 return (ty/2U)*xt*2U+group*4U+within;
}
#endif
