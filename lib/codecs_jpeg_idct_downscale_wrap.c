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

void  jpeg_idct_downscale_wrap_islow (j_decompress_ptr cinfo, jpeg_component_info * compptr,
                                      JCOEFPTR coef_block,
                                      JSAMPARRAY output_buf, JDIMENSION output_col);

static inline float fast_linear_to_srgb(float x)
{
    // Gamma correction
    // http://www.4p8.com/eric.brasseur/gamma.html#formulas
    if (x < 0.0f) return 0;
    if (x > 1.0f) return 255.0f;
    float r = 255.0f * fasterpow(x, 1.0f / 2.2f);
    //return 255.0f * (float)pow(clr, 1.0f / 2.2f);
    //printf("Linear %f to srgb %f\n", x, r);
    return r;
}

//THIS IS THE HOTSPOT - 90% of performance to be extracted is here
//https://stackoverflow.com/questions/6475373/optimizations-for-pow-with-const-non-integer-exponent
//Chebychev approximations could likely eliminate this hotspot
static inline float fast_srgb_to_linear(uint8_t s)
{
    if (s > 255.0f) return 1.0f;
    if (s < 0.0f) return 0.0f;
    //return (float)pow(s / 255.0f, 2.2f);

    float r = fasterpow((float)s / 255.0f, 2.2f);
    //printf("Srgb %f to linear %f\n", s, r);
    return r;
}



void  jpeg_idct_downscale_wrap_islow (j_decompress_ptr cinfo, jpeg_component_info * compptr,
                                    JCOEFPTR coef_block,
                                    JSAMPARRAY output_buf, JDIMENSION output_col){


    JSAMPLE intermediate[DCTSIZE2];
    JSAMPROW rows[DCTSIZE];
    JSAMPROW outptr;
    JSAMPLE *range_limit = cinfo->sample_range_limit;
    int i, ctr, ctr_x, linear_light_y, linear_light_x;

    for (i =0; i < DCTSIZE; i++)
        rows[i] = &intermediate[i * DCTSIZE];


    jpeg_idct_islow(cinfo, compptr, coef_block, &rows[0], 0);


    float linear[DCTSIZE2];

    //TODO: use LUT
    for (i =0; i < DCTSIZE2; i++)
        linear[i] = fast_srgb_to_linear(intermediate[i]);

#if JPEG_LIB_VERSION >= 70
    int scaled = compptr->DCT_h_scaled_size;
#else
    int scaled = compptr->DCT_scaled_size;
#endif
    float * linear_light_ptr = &linear[0];

    //Downscale and set output values
    //Inlining and permitting those 4 loops to be unrolled
    //Didn't actually help too much, but it probably will
    //On less advanced compilers.
#define SCALE_DOWN(target_size, input_pixels_window) \
  for (ctr = 0; ctr < target_size; ctr++) { \
    outptr = output_buf[ctr] + output_col; \
    for (ctr_x = 0; ctr_x < target_size; ctr_x++){ \
      linear_light_ptr = &linear[ctr * input_pixels_window * DCTSIZE \
                                        + ctr_x * input_pixels_window]; \
      float sum = 0;                                                    \
      for (linear_light_y = 0; linear_light_y < input_pixels_window; linear_light_y++){ \
        for (linear_light_x = 0; linear_light_x < input_pixels_window; linear_light_x++){ \
          sum += linear_light_ptr[linear_light_x]; \
        } \
        linear_light_ptr += DCTSIZE; \
      }  \
      outptr[ctr_x] = range_limit[((int)linear_to_srgb(sum / (float)(input_pixels_window * input_pixels_window))) & RANGE_MASK]; \
    } \
  } \


    if (scaled == 1) {
        SCALE_DOWN(1, 8)
    } else if (scaled == 2) {
        SCALE_DOWN(2, 4)
    } else if (scaled == 4) {
        SCALE_DOWN(4, 2)
    } else {
        exit (42);
    }

}
