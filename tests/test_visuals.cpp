#include "catch.hpp"
#include "helpers_visual.h"
//#define FLOW_STORE_CHECKSUMS

#ifdef FLOW_STORE_CHECKSUMS
bool store_checksums = true;
#else
bool store_checksums = false;
#endif

TEST_CASE("Test fill_rect", "")
{
    flow_c * c = flow_context_create();
    struct flow_graph * g = flow_graph_create(c, 10, 10, 200, 2.0);
    ERR(c);
    struct flow_bitmap_bgra * b;
    int32_t last;

    last = flow_node_create_canvas(c, &g, -1, flow_bgra32, 400, 300, 0xFFFFFFFF);
    last = flow_node_create_fill_rect(c, &g, last, 0, 0, 50, 100, 0xFF0000FF);
    last = flow_node_create_bitmap_bgra_reference(c, &g, last, &b);
    struct flow_job * job = flow_job_create(c);
    ERR(c);
    if (!flow_job_execute(c, job, &g)) {
        ERR(c);
    }

    REQUIRE(visual_compare(c, b, "FillRect", store_checksums, __FILE__, __func__, __LINE__) == true);
    ERR(c);
    flow_context_destroy(c);
}

TEST_CASE("Test scale image", "")
{

    flow_c * c = flow_context_create();
    size_t bytes_count = 0;
    uint8_t * bytes = get_bytes_cached(c, &bytes_count, "http://www.rollthepotato.net/~john/kevill/test_800x600.jpg");
    REQUIRE(djb2_buffer(bytes, bytes_count) == 0x8ff8ec7a8539a2d5); // Test the checksum. I/O can be flaky

    struct flow_job * job = flow_job_create(c);
    ERR(c);
    int32_t input_placeholder = 0;
    struct flow_io * input = flow_io_create_from_memory(c, flow_io_mode_read_seekable, bytes, bytes_count, job, NULL);
    flow_job_add_io(c, job, input, input_placeholder, FLOW_INPUT);

    struct flow_graph * g = flow_graph_create(c, 10, 10, 200, 2.0);
    ERR(c);
    struct flow_bitmap_bgra * b;
    int32_t last;

    last = flow_node_create_decoder(c, &g, -1, input_placeholder);
    // flow_node_set_decoder_downscale_hint(c, g, last, 400,300,400,300);
    last = flow_node_create_scale(c, &g, last, 400, 300);
    last = flow_node_create_bitmap_bgra_reference(c, &g, last, &b);
    ERR(c);
    if (!flow_job_execute(c, job, &g)) {
        ERR(c);
    }

    REQUIRE(visual_compare(c, b, "ScaleThePotato", store_checksums, __FILE__, __func__, __LINE__) == true);
    ERR(c);
    flow_context_destroy(c);
}

bool scale_down(flow_c * c, flow_job * job, uint8_t * bytes, size_t bytes_count, int block_scale_to_x,
                int block_scale_to_y, int scale_to_x, int scale_to_y, flow_bitmap_bgra ** ref)
{
    int32_t input_placeholder = 0;
    struct flow_io * input = flow_io_create_from_memory(c, flow_io_mode_read_seekable, bytes, bytes_count, job, NULL);
    flow_job_add_io(c, job, input, input_placeholder, FLOW_INPUT);

    struct flow_graph * g = flow_graph_create(c, 10, 10, 200, 2.0);
    ERR(c);
    struct flow_bitmap_bgra * b;
    int32_t last;

    last = flow_node_create_decoder(c, &g, -1, input_placeholder);

    if (block_scale_to_x > 0) {
        if (!flow_job_decoder_set_downscale_hints_by_placeholder_id(
                c, job, input_placeholder, block_scale_to_x, block_scale_to_y, block_scale_to_x, block_scale_to_y)) {
            ERR(c);
        }
    }
    struct flow_decoder_info info;
    if (!flow_job_get_decoder_info(c, job, input_placeholder, &info)) {
        ERR(c);
    }
    last = flow_node_create_primitive_crop(c, &g, last, 0, 0, info.frame0_width, info.frame0_height);
    last = flow_node_create_scale(c, &g, last, scale_to_x, scale_to_y);
    last = flow_node_create_bitmap_bgra_reference(c, &g, last, ref);
    ERR(c);
    if (!flow_job_execute(c, job, &g)) {
        ERR(c);
    }

    return true;
}

TEST_CASE("Test 8->4 downscaling contrib windows",""){
    flow_c * c = flow_context_create();
    struct flow_interpolation_details * details = flow_interpolation_details_create_bicubic_custom(
        c, 2, 1. / 1.1685777620836932, 0.37821575509399867, 0.31089212245300067);

    struct flow_interpolation_line_contributions * contrib = flow_interpolation_line_contributions_create(c, 4, 8, details);


    REQUIRE(contrib->ContribRow[0].Weights[0] == Approx(0.45534f));
    REQUIRE(contrib->ContribRow[3].Weights[contrib->ContribRow[3].Right - contrib->ContribRow[3].Left -1] == Approx(0.45534f));
    flow_context_destroy(c);
}
TEST_CASE("Test downscale image during decoding", "")
{

    flow_c * c = flow_context_create();
    size_t bytes_count = 0;
    uint8_t * bytes = get_bytes_cached(c, &bytes_count, "http://www.rollthepotato.net/~john/kevill/test_800x600.jpg");
    REQUIRE(djb2_buffer(bytes, bytes_count) == 0x8ff8ec7a8539a2d5); // Test the checksum. I/O can be flaky

    struct flow_job * job_a = flow_job_create(c);
    struct flow_job * job_b = flow_job_create(c);
    struct flow_bitmap_bgra * bitmap_a;
    struct flow_bitmap_bgra * bitmap_b;
    ERR(c);
    if (!scale_down(c, job_a, bytes, bytes_count, 400, 300, 400, 300, &bitmap_a)) {
        ERR(c);
    }
    if (!scale_down(c, job_b, bytes, bytes_count, 0, 0, 400, 300, &bitmap_b)) {
        ERR(c);
    }
    // We are using an 'ideal' scaling of the full image as a control
    // Under srgb decoding (libjpeg-turbo as-is ISLOW downsampling), DSSIM=0.003160
    // Under linear light decoder box downsampling (vs linear light true resampling), DSSIM=0.002947
    // Using the flow_bitmap_float scaling in two dierctions, DSSIM=0.000678


    REQUIRE(visual_compare_two(c, bitmap_a, bitmap_b, "Compare ideal downscaling vs downscaling in decoder", __FILE__,
                               __func__, __LINE__) == true);
    ERR(c);
    flow_context_destroy(c);
}
