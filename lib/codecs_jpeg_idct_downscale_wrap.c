#include <stdio.h>
#include "imageflow_private.h"
#include "lcms2.h"
#include "codecs.h"

#define JPEG_INTERNALS
#include "libjpeg-turbo_private/jpeglib.h"
#include "libjpeg-turbo_private/jinclude.h"
#include "libjpeg-turbo_private/jconfigint.h" /* Private declarations for DCT subsystem */

//#include "libjpeg-turbo_private/jmorecfg.h" /* Private declarations for DCT subsystem */
#include "libjpeg-turbo_private/jdct.h" /* Private declarations for DCT subsystem */

#include "fastapprox.h"

void jpeg_idct_downscale_wrap_islow(j_decompress_ptr cinfo, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                    JSAMPARRAY output_buf, JDIMENSION output_col);

static inline float fast_linear_to_srgb(float x)
{
    // Gamma correction
    // http://www.4p8.com/eric.brasseur/gamma.html#formulas
    if (x < 0.0f)
        return 0;
    if (x > 1.0f)
        return 255.0f;
    float r = 255.0f * fasterpow(x, 1.0f / 2.2f);
    // return 255.0f * (float)pow(clr, 1.0f / 2.2f);
    // printf("Linear %f to srgb %f\n", x, r);
    return r;
}

// THIS IS THE HOTSPOT - 90% of performance to be extracted is here
// https://stackoverflow.com/questions/6475373/optimizations-for-pow-with-const-non-integer-exponent
// Chebychev approximations could likely eliminate this hotspot
static inline float fast_srgb_to_linear(uint8_t s)
{
    float r = fasterpow((float)s / 255.0f, 2.2f);
    // printf("Srgb %f to linear %f\n", s, r);
    return r;
}

void jpeg_idct_downscale_wrap_islow(j_decompress_ptr cinfo, jpeg_component_info * compptr, JCOEFPTR coef_block,
                                    JSAMPARRAY output_buf, JDIMENSION output_col)
{

    JSAMPLE intermediate[DCTSIZE2];
    JSAMPROW rows[DCTSIZE];
    JSAMPROW outptr;
    //JSAMPLE * range_limit = cinfo->sample_range_limit;
    int i, ctr, ctr_x;

    for (i = 0; i < DCTSIZE; i++)
        rows[i] = &intermediate[i * DCTSIZE];

    jpeg_idct_islow(cinfo, compptr, coef_block, &rows[0], 0);


#if JPEG_LIB_VERSION >= 70
    int scaled = compptr->DCT_h_scaled_size;
#else
    int scaled = compptr->DCT_scaled_size;
#endif

    flow_c * c = flow_context_create();

    struct flow_interpolation_details * details = flow_interpolation_details_create_bicubic_custom(
        c, 2, 1. / 1.1685777620836932, 0.37821575509399867, 0.31089212245300067);

    struct flow_interpolation_line_contributions * contrib = flow_interpolation_line_contributions_create(c, scaled, DCTSIZE, details);
    if (contrib == NULL) {
        FLOW_add_to_callstack(c);
        exit(99);
    }

    struct flow_bitmap_float * source_buf = flow_bitmap_float_create(c, DCTSIZE, DCTSIZE, flow_gray8, false);

    // TODO: use LUT
    for (i = 0; i < DCTSIZE2 && i < (int)source_buf->float_count; i++)
        source_buf->pixels[i] =Context_srgb_to_floatspace(c, intermediate[i]);
            //srgb_to_linear((float)intermediate[i] / 255.0f);


    struct flow_bitmap_float * dest_buf = flow_bitmap_float_create(c, scaled, DCTSIZE, flow_gray8, false);

    if (!flow_bitmap_float_scale_rows(c, source_buf, 0, dest_buf, 0, DCTSIZE, contrib->ContribRow)) {
        exit(99);
    }

    struct flow_bitmap_float * transposed_buf = flow_bitmap_float_create(c, DCTSIZE, scaled, flow_gray8, false);

    for (int y =0; y < DCTSIZE; y++){
        for (int x = 0; x < scaled; x++){
            transposed_buf->pixels[transposed_buf->float_stride * x + y] = dest_buf->pixels[x + y * dest_buf->float_stride];
        }
    }


    struct flow_bitmap_float * final_buf = flow_bitmap_float_create(c, scaled, scaled, flow_gray8, false);

    if (!flow_bitmap_float_scale_rows(c, transposed_buf, 0, final_buf, 0, scaled, contrib->ContribRow)) {
        exit(99);
    }

    //int input_pixels_window =  8 / scaled;
    int target_size = scaled;
    for (ctr = 0; ctr < target_size; ctr++) {
        outptr = output_buf[ctr] + output_col;
        for (ctr_x = 0; ctr_x < target_size; ctr_x++) {

            float pixel =final_buf->pixels[ctr_x * final_buf->float_stride + ctr];
            outptr[ctr_x] = Context_floatspace_to_srgb(c, pixel);
        }
    }


    flow_context_destroy(c);
}
