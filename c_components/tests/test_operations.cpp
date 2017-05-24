#include <lib/trim_whitespace.h>
#include "catch.hpp"

// TODO: Test with opaque and transparent images
// TODO: Test using random dots instead of rectangles to see if overlaps are correct.

flow_rect test_detect_content_for(uint32_t w, uint32_t h, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
                                  uint32_t color_srgb_argb, flow_pixel_format fmt)
{
    flow_c * c = flow_context_create();

    flow_bitmap_bgra * b = flow_bitmap_bgra_create(c, w, h, true, fmt);

    flow_bitmap_bgra_fill_rect(c, b, 0, 0, w, h, 0xFF000000);
    flow_bitmap_bgra_fill_rect(c, b, x1, y1, x2, y2, color_srgb_argb);

    char path[256];
    flow_snprintf(&path[0], sizeof(path), "rect_%i_%i_%i_%i.png", x1, y1, x2, y2);
    flow_bitmap_bgra_save_png(c, b, &path[0]);

    flow_context_print_and_exit_if_err(c);

    flow_rect r = detect_content(c, b, 1);
    flow_context_print_and_exit_if_err(c);

    flow_context_destroy(c);
    return r;
}

TEST_CASE("Exhaustive test of detect_content for small images", "")
{
    flow_c * c = flow_context_create();

    flow_rect r;
    for (int w = 3; w < 12; w++) {
        for (int h = 3; h < 12; h++) {
            for (int fmt_ix = 0; fmt_ix < 2; fmt_ix++) {
                flow_bitmap_bgra * b = flow_bitmap_bgra_create(c, w, h, true, fmt_ix == 0 ? flow_bgra32 : flow_bgr24);

                for (int x = 0; x < w; x++) {
                    for (int y = 0; y < h; y++) {

                        if (x == 1 && y == 1 && w == 3 && h == 3) {
                            continue;
                            // This is a checkerboard, we don't support them
                        }
                        flow_bitmap_bgra_fill_rect(c, b, 0, 0, w, h, 0xFF000000);

                        flow_bitmap_bgra_fill_rect(c, b, x, y, x + 1, y + 1, 0xFF0000FF);

                        flow_context_print_and_exit_if_err(c);

                        flow_rect r = detect_content(c, b, 1);

                        bool correct = ((r.x1 == x) && (r.y1 == y) && (r.x2 == x + 1) && (r.y2 == y + 1));

                        if (!correct) {
                            r = detect_content(c, b, 1);
                            flow_bitmap_bgra_save_png(c, b, "failed_detect_content.png");
                            CAPTURE(w);
                            CAPTURE(h);
                            CAPTURE(x);
                            CAPTURE(y);
                            CAPTURE(r.x1);
                            CAPTURE(r.y1);
                            CAPTURE(r.x2);
                            CAPTURE(r.y2);
                            CAPTURE(b->fmt);

                            CAPTURE(b->alpha_meaningful);
                            REQUIRE(r.x2 == x + 1);
                            REQUIRE(r.y2 == y + 1);
                            REQUIRE(r.x1 == x);
                            REQUIRE(r.y1 == y);
                        }
                    }
                }
                FLOW_destroy(c, b);
            }
        }
    }

    flow_context_destroy(c);
}

TEST_CASE("Test detect_content", "")
{
    flow_rect r;

    r = test_detect_content_for(10, 10, 1, 1, 9, 9, 0xFF0000FF, flow_bgra32);

    CAPTURE(r.x1);
    CAPTURE(r.y1);
    CAPTURE(r.x2);
    CAPTURE(r.y2);
    REQUIRE(r.x2 == 9);
    REQUIRE(r.y2 == 9);
    REQUIRE(r.x1 == 1);
    REQUIRE(r.y1 == 1);

    r = test_detect_content_for(100, 100, 2, 3, 70, 70, 0xFF0000FF, flow_bgra32);

    CAPTURE(r.x1);
    CAPTURE(r.y1);
    CAPTURE(r.x2);
    CAPTURE(r.y2);
    REQUIRE(r.x2 == 70);
    REQUIRE(r.y2 == 70);
    REQUIRE(r.x1 == 2);
    REQUIRE(r.y1 == 3);
}


TEST_CASE("Test thresholding by drawing gradients", "")
{

    int w = 300;
    int h = 300;
    flow_c * c = flow_context_create();
    flow_bitmap_bgra * b = flow_bitmap_bgra_create(c, w, h, true, flow_bgra32);
    flow_context_print_and_exit_if_err(c);

    for (int step = 1;  step < 256; step++){
        for (int dir = 0; dir < 4; dir++){
            flow_bitmap_bgra_fill_rect(c, b, 0, 0, w, h, 0xFFFFFFFF);
            flow_context_print_and_exit_if_err(c);

            int margin = 0;
            int color = 255 - step;
            int pixel;
            for (pixel = 0; pixel < 256 && color >= 0; pixel++){

                int primary = margin + pixel;

                int line_width = 1;
                if (color < step) {
                    //last draw should fill it in
                    line_width = (((dir % 2) == 0) ? h : w) - margin - margin - pixel - 20;
                }


                int x1,y1,x2,y2;

                switch (dir){
                    case 0: x1 = primary; break;
                    case 1: y1 = primary; break;
                    case 2: x1 = w - primary - line_width; break;
                    case 3: y1 = h - primary - line_width; break;
                }



                if ((dir % 2) == 0){
                    y1 = margin;
                    y2 = h - margin;
                    x2 = x1 + line_width;
                }else{
                    x1 = margin;
                    x2 = w - margin;
                    y2 = y1 + line_width;
                }

                int full_color = (0xFF << 24) | color | (color << 8) | (color << 16);

                flow_bitmap_bgra_fill_rect(c, b,x1,y1,x2,y2, full_color);
                flow_context_print_and_exit_if_err(c);
                color -= step;
            }


            for (int threshold = step; threshold < 8 * step; threshold+=2) {
                flow_rect r = detect_content(c, b, threshold);
                flow_context_print_and_exit_if_err(c);

                int detected_primary;
                int e[3];

                switch (dir) {
                    case 0:
                        detected_primary = r.x1;
                        e[0] = r.y1, e[1] = w - r.x2, e[2] = h - r.y2;
                        break;
                    case 1:
                        detected_primary = r.y1;
                        e[0] = r.x1, e[2] = w - r.x2, e[1] = h - r.y2;
                        break;
                    case 2:
                        detected_primary = w - r.x2;
                        e[0] = r.y1, e[1] = r.x1, e[2] = h - r.y2;
                        break;
                    case 3:
                        detected_primary = h - r.y2;
                        e[1] = r.y1, e[0] = w - r.x2, e[2] = r.x1;
                        break;
                }


                if (threshold < 2 * step){
                    REQUIRE(detected_primary == margin);
                    REQUIRE(e[0] == margin);
                    REQUIRE(e[2] == margin);
                }else if (detected_primary != margin + pixel) {
                    CAPTURE(dir);
                    //CAPTURE(r);
                    CAPTURE(threshold);
                    CAPTURE(pixel);
                    CAPTURE(step);
                    CAPTURE(detected_primary);
                    //              char path[256];
                    //flow_snprintf(&path[0], sizeof(path), "detect_cradient.png", x1, y1, x2, y2);
                    flow_bitmap_bgra_save_png(c, b, "detect_gradient.png");

                    REQUIRE(e[0] == margin);
                    REQUIRE(e[2] == margin);
                    REQUIRE(false);
                }
            }




        }
    }


    flow_context_destroy(c);

}
// TODO: Compare to a reference scaling

typedef void (*blockscale_fn)(uint8_t input[64], uint8_t ** output_rows, uint32_t output_col);

blockscale_fn blockscale_funclist[]
    = { flow_scale_spatial_srgb_7x7, flow_scale_spatial_srgb_6x6, flow_scale_spatial_srgb_5x5,
        flow_scale_spatial_srgb_4x4, flow_scale_spatial_srgb_3x3, flow_scale_spatial_srgb_2x2,
        flow_scale_spatial_srgb_1x1, flow_scale_spatial_7x7,      flow_scale_spatial_6x6,
        flow_scale_spatial_5x5,      flow_scale_spatial_4x4,      flow_scale_spatial_3x3,
        flow_scale_spatial_2x2,      flow_scale_spatial_1x1 };

TEST_CASE("Test block downscaling", "")
{

    uint8_t input[64];
    memset(&input[0], 0, 64);
    uint8_t output[64];
    uint8_t * rows[8] = { &output[0],     &output[8],     &output[8 * 2], &output[8 * 3],
                          &output[8 * 4], &output[8 * 5], &output[8 * 6], &output[8 * 7] };

    for (size_t i = 0; i < sizeof(blockscale_funclist) / sizeof(blockscale_fn); i++) {
        blockscale_funclist[i](input, rows, 0);
    }
}

TEST_CASE("Benchmark block downscaling", "")
{

    uint8_t input[64];
    memset(&input[0], 0, 64);
    uint8_t output[64];
    uint8_t * rows[8] = { &output[0],     &output[8],     &output[8 * 2], &output[8 * 3],
                          &output[8 * 4], &output[8 * 5], &output[8 * 6], &output[8 * 7] };

    for (size_t i = 0; i < sizeof(blockscale_funclist) / sizeof(blockscale_fn); i++) {
#ifdef DEBUG
        int reps = 90000;
#else
        int reps = 900;
#endif
        int64_t start = flow_get_high_precision_ticks();
        for (int j = 0; j < reps; j++) {
            blockscale_funclist[i](input, rows, 0);
        }
        double ms = (flow_get_high_precision_ticks() - start) * 1000.0 / (float)flow_get_profiler_ticks_per_second();
        fprintf(stdout, "Block downscaling fn %d took %.05fms for %d reps (%0.2f megapixels)\n", (int)i, ms, reps,
                (float)(reps * 64) / 1000000.0f);
    }
}
