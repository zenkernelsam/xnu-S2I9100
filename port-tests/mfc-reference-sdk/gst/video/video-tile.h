/* Host-only declaration facade for the retained original reference function.
 * The reference file is never linked into XNU or a production module. */
#include <stddef.h>
#include <assert.h>
typedef unsigned guint;
typedef int gint;
typedef size_t gsize;
typedef unsigned GstVideoTileMode;
#define GST_VIDEO_TILE_MODE_ZFLIPZ_2X2 1U
#define GST_VIDEO_TILE_MODE_LINEAR 2U
#define GST_VIDEO_TILE_MODE_IS_INDEXED(mode) ((mode)==1U||(mode)==2U)
#define GST_ROUND_DOWN_2(n) ((n)&~1)
#define GST_ROUND_DOWN_4(n) ((n)&~3)
#define g_return_val_if_fail(test,value) do {if(!(test))return (value);}while(0)
guint gst_video_tile_get_index(GstVideoTileMode,gint,gint,gint,gint);
