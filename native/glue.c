#include "sokol_gfx.h"
#include "sokol_log.h"
#include "util/sokol_gl.h"
#include "util/sokol_debugtext.h"
#include "glue.h"

#define GLUE_TEXT_SIZES 3

static int glue_samples;
static sgl_pipeline glue_blended;
static sdtx_context glue_texts[GLUE_TEXT_SIZES];

void glue_setup(int samples) {
    glue_samples = samples;

    sg_setup(&(sg_desc){
        .environment.defaults = {
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
            .sample_count = samples,
        },
        .logger.func = slog_func,
    });

    sgl_setup(&(sgl_desc_t){
        .max_vertices = 1 << 18,
        .logger.func = slog_func,
    });
    glue_blended = sgl_make_pipeline(&(sg_pipeline_desc){
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
    });

    sdtx_setup(&(sdtx_desc_t){
        .fonts = {
            [0] = sdtx_font_kc853(),
        },
        .logger.func = slog_func,
    });

    for (int i = 0; i < GLUE_TEXT_SIZES; i++) {
        glue_texts[i] = sdtx_make_context(&(sdtx_context_desc_t){ .char_buf_size = 1024 });
    }
}

void glue_begin(int width, int height, float r, float g, float b) {
    sg_begin_pass(&(sg_pass){
        .action.colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { r, g, b, 1.0f },
        },
        .swapchain = {
            .width = width,
            .height = height,
            .sample_count = glue_samples,
            .color_format = SG_PIXELFORMAT_RGBA8,
            .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
            .gl.framebuffer = 0,
        },
    });
    sgl_defaults();
    sgl_load_pipeline(glue_blended);
}

void glue_viewport(int x, int y, int width, int height) {
    sg_apply_viewport(x, y, width, height, true);
}

void glue_text(int size) {
    if (size >= 0 && size < GLUE_TEXT_SIZES) {
        sdtx_set_context(glue_texts[size]);
    }
}

void glue_end(void) {
    sgl_draw();
    for (int i = 0; i < GLUE_TEXT_SIZES; i++) {
        sdtx_context_draw(glue_texts[i]);
    }

    sg_end_pass();
    sg_commit();
}

void glue_shutdown(void) {
    sdtx_shutdown();
    sgl_shutdown();
    sg_shutdown();
}
