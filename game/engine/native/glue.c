#include "sokol_gfx.h"
#include "sokol_log.h"
#include "util/sokol_gl.h"
#include "util/sokol_debugtext.h"
#include "glue.h"

#define GLUE_TEXT_SIZES 3
#define GLUE_LAYERS 2
#define GLUE_CRT_LINES 200

static int glue_samples;
static int glue_current_layer;
static sgl_pipeline glue_blended;
static sdtx_context glue_texts[GLUE_TEXT_SIZES];

static int glue_width, glue_height;
static float glue_background[3];

// With the CRT look on, the game is drawn into an image first, and the image
// onto the window through a shader. The image covers the whole window, bars and
// all, so the old television fills the screen in one piece.
static bool glue_crt_on;
static int glue_crt_width, glue_crt_height;
static float glue_crt_line, glue_crt_top;
static sg_image glue_crt_color, glue_crt_resolve, glue_crt_depth;
static sg_view glue_crt_color_view, glue_crt_resolve_view, glue_crt_depth_view, glue_crt_texture;
static sg_sampler glue_crt_sampler;
static sg_pipeline glue_crt_pipeline;

static const char* glue_crt_vertex =
    "#version 410\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    uv = corner;\n"
    "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char* glue_crt_fragment =
    "#version 410\n"
    "uniform sampler2D screen;\n"
    "uniform vec4 crt;\n"
    "in vec2 uv;\n"
    "out vec4 frag_color;\n"
    "vec2 bulge(vec2 at) {\n"
    "    vec2 centered = at * 2.0 - 1.0;\n"
    "    vec2 bend = abs(centered.yx) / vec2(7.0, 6.0);\n"
    "    return (centered + centered * bend * bend) * 0.5 + 0.5;\n"
    "}\n"
    "void main() {\n"
    "    vec2 at = bulge(uv);\n"
    "    if (at.x < 0.0 || at.x > 1.0 || at.y < 0.0 || at.y > 1.0) {\n"
    "        frag_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 pixel = 1.0 / crt.xy;\n"
    "    vec3 color = vec3(\n"
    "        texture(screen, at + vec2(pixel.x * 1.2, 0.0)).r,\n"
    "        texture(screen, at).g,\n"
    "        texture(screen, at - vec2(pixel.x * 1.2, 0.0)).b);\n"
    "    vec3 glow = texture(screen, at + pixel * vec2(3.0, 0.0)).rgb\n"
    "        + texture(screen, at - pixel * vec2(3.0, 0.0)).rgb\n"
    "        + texture(screen, at + pixel * vec2(0.0, 3.0)).rgb\n"
    "        + texture(screen, at - pixel * vec2(0.0, 3.0)).rgb;\n"
    "    color += glow * 0.07;\n"
    "    float row = fract((at.y * crt.y - crt.w) / crt.z);\n"
    "    float beam = sqrt(0.5 - 0.5 * cos(row * 6.28318));\n"
    "    float luma = clamp(dot(color, vec3(0.3, 0.59, 0.11)), 0.0, 1.0);\n"
    "    float scanline = 1.0 - (0.65 - 0.3 * luma) * (1.0 - beam);\n"
    "    float edge = 16.0 * at.x * at.y * (1.0 - at.x) * (1.0 - at.y);\n"
    "    float vignette = pow(edge, 0.22);\n"
    "    frag_color = vec4(color * scanline * vignette * 1.22, 1.0);\n"
    "}\n";

static sg_swapchain glue_swapchain(void) {
    return (sg_swapchain){
        .width = glue_width,
        .height = glue_height,
        .sample_count = glue_samples,
        .color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .gl.framebuffer = 0,
    };
}

static sg_pass_action glue_clear(void) {
    return (sg_pass_action){
        .colors[0] = {
            .load_action = SG_LOADACTION_CLEAR,
            .clear_value = { glue_background[0], glue_background[1], glue_background[2], 1.0f },
        },
    };
}

static void glue_crt_setup(void) {
    glue_crt_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });

    sg_shader shader = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = glue_crt_vertex,
        .fragment_func.source = glue_crt_fragment,
        .uniform_blocks[0] = {
            .stage = SG_SHADERSTAGE_FRAGMENT,
            .size = 4 * sizeof(float),
            .glsl_uniforms[0] = { .type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "crt" },
        },
        .views[0].texture = {
            .stage = SG_SHADERSTAGE_FRAGMENT,
            .image_type = SG_IMAGETYPE_2D,
            .sample_type = SG_IMAGESAMPLETYPE_FLOAT,
        },
        .samplers[0] = {
            .stage = SG_SHADERSTAGE_FRAGMENT,
            .sampler_type = SG_SAMPLERTYPE_FILTERING,
        },
        .texture_sampler_pairs[0] = {
            .stage = SG_SHADERSTAGE_FRAGMENT,
            .view_slot = 0,
            .sampler_slot = 0,
            .glsl_name = "screen",
        },
    });

    glue_crt_pipeline = sg_make_pipeline(&(sg_pipeline_desc){ .shader = shader });
}

static void glue_crt_resize(int width, int height) {
    if (width == glue_crt_width && height == glue_crt_height) {
        return;
    }

    if (glue_crt_width > 0) {
        sg_destroy_view(glue_crt_texture);
        sg_destroy_view(glue_crt_depth_view);
        sg_destroy_view(glue_crt_resolve_view);
        sg_destroy_view(glue_crt_color_view);
        sg_destroy_image(glue_crt_depth);
        sg_destroy_image(glue_crt_resolve);
        sg_destroy_image(glue_crt_color);
    }

    glue_crt_width = width;
    glue_crt_height = height;
    glue_crt_color = sg_make_image(&(sg_image_desc){
        .usage.color_attachment = true,
        .width = width,
        .height = height,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .sample_count = glue_samples,
    });
    glue_crt_resolve = sg_make_image(&(sg_image_desc){
        .usage.resolve_attachment = true,
        .width = width,
        .height = height,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .sample_count = 1,
    });
    glue_crt_depth = sg_make_image(&(sg_image_desc){
        .usage.depth_stencil_attachment = true,
        .width = width,
        .height = height,
        .pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL,
        .sample_count = glue_samples,
    });
    glue_crt_color_view = sg_make_view(&(sg_view_desc){ .color_attachment.image = glue_crt_color });
    glue_crt_resolve_view = sg_make_view(&(sg_view_desc){ .resolve_attachment.image = glue_crt_resolve });
    glue_crt_depth_view = sg_make_view(&(sg_view_desc){ .depth_stencil_attachment.image = glue_crt_depth });
    glue_crt_texture = sg_make_view(&(sg_view_desc){ .texture.image = glue_crt_resolve });
}

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

    glue_crt_setup();
}

void glue_crt(int on) {
    glue_crt_on = on != 0;
}

void glue_begin(int width, int height, int x, int y, int area_width, int area_height, float r, float g, float b) {
    glue_width = width;
    glue_height = height;
    glue_background[0] = r;
    glue_background[1] = g;
    glue_background[2] = b;

    if (glue_crt_on) {
        glue_crt_resize(width, height);
        glue_crt_line = area_height / (float)GLUE_CRT_LINES;
        glue_crt_top = (float)y;
        sg_begin_pass(&(sg_pass){
            .action = glue_clear(),
            .attachments = {
                .colors[0] = glue_crt_color_view,
                .resolves[0] = glue_crt_resolve_view,
                .depth_stencil = glue_crt_depth_view,
            },
        });
        sg_apply_viewport(x, y, area_width, area_height, true);
    } else {
        sg_begin_pass(&(sg_pass){ .action = glue_clear(), .swapchain = glue_swapchain() });
        sg_apply_viewport(x, y, area_width, area_height, true);
    }

    sgl_defaults();
    sgl_load_pipeline(glue_blended);
    glue_layer(0);
}

void glue_layer(int layer) {
    if (layer >= 0 && layer < GLUE_LAYERS) {
        glue_current_layer = layer;
        sgl_layer(layer);
    }
}

void glue_text(int size) {
    if (size >= 0 && size < GLUE_TEXT_SIZES) {
        sdtx_set_context(glue_texts[size]);
        sdtx_layer(glue_current_layer);
    }
}

void glue_end(void) {
    // Text goes over the shapes in its own layer, and every layer over the ones
    // before it.
    for (int layer = 0; layer < GLUE_LAYERS; layer++) {
        sgl_draw_layer(layer);
        for (int i = 0; i < GLUE_TEXT_SIZES; i++) {
            sdtx_context_draw_layer(glue_texts[i], layer);
        }
    }

    sg_end_pass();

    if (glue_crt_on) {
        sg_begin_pass(&(sg_pass){ .action = glue_clear(), .swapchain = glue_swapchain() });
        sg_apply_viewport(0, 0, glue_width, glue_height, true);
        sg_apply_pipeline(glue_crt_pipeline);
        sg_apply_bindings(&(sg_bindings){
            .views[0] = glue_crt_texture,
            .samplers[0] = glue_crt_sampler,
        });
        float size[4] = { (float)glue_width, (float)glue_height, glue_crt_line, glue_crt_top };
        sg_apply_uniforms(0, &SG_RANGE(size));
        sg_draw(0, 3, 1);
        sg_end_pass();
    }

    sg_commit();
}

void glue_shutdown(void) {
    sdtx_shutdown();
    sgl_shutdown();
    sg_shutdown();
}
