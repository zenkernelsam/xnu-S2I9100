#ifndef IOS7LAB_DECMPFS_DECODE_H
#define IOS7LAB_DECMPFS_DECODE_H
/* decmpfs type 3/4 permits a 0xff-prefixed stored block when deflate
 * would not save space. The caller supplies zlib allocation callbacks. */
static int ios7lab_decmpfs_decode(z_stream *zs, const unsigned char *input,
    uInt input_size, unsigned char *output, uInt output_size, uInt *produced)
{
    int result;
    *produced = 0;
    if (!input_size) return Z_DATA_ERROR;
    if (input[0] == 0xff) {
        if (input_size - 1 > output_size) return Z_BUF_ERROR;
        memcpy(output, input + 1, input_size - 1);
        *produced = input_size - 1;
        return Z_OK;
    }
    zs->next_in = (Bytef *)input;
    zs->avail_in = input_size;
    zs->next_out = (Bytef *)output;
    zs->avail_out = output_size;
    result = inflateInit(zs);
    if (result != Z_OK) return result;
    result = inflate(zs, Z_FINISH);
    *produced = zs->total_out;
    inflateEnd(zs);
    return result == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}
#endif
