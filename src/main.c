#define SOKOL_IMPL
#define SOKOL_NO_ENTRY
#define SOKOL_GLCORE
#include "sokol_app.h"
#include "sokol_audio.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "agnes.h"
#include <stdio.h>

sg_pass_action pass_action = {0};
sg_buffer vbuf = {0};
sg_buffer ibuf = {0};
sg_pipeline pip = {0};
sg_bindings bind = {0};
sg_image img = {0};

agnes_t* nes;
agnes_input_t controller1 = {0};
static void audioCallback(float* buffer, int num_frames, int num_channels, void* user_data);
static void* read_file(const char *filename, size_t *out_len);

void init() {
    sg_desc desc = {0};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);

    saudio_desc as_desc = {0};
    as_desc.logger.func = slog_func;
    as_desc.buffer_frames = 1024;
    // as_desc.num_channels = 2;
    as_desc.stream_userdata_cb = audioCallback;
    as_desc.user_data = nes;
    saudio_setup(&as_desc);
    assert(as_desc.user_data);
    assert(saudio_channels() == 1);

    const float vertices[] = {
            // positions     uv
            -1.0, -1.0, 0.0, 0.0, 1.0,
            1.0,  -1.0, 0.0, 1.0, 1.0,
            1.0,  1.0,  0.0, 1.0, 0.0,
            -1.0, 1.0,  0.0, 0.0, 0.0,
    };
    sg_buffer_desc vb_desc = {0};
    vb_desc.data = SG_RANGE(vertices);
    vbuf = sg_make_buffer(&vb_desc);

    const int indices[] = { 0, 1, 2, 0, 2, 3, };
    sg_buffer_desc ib_desc = {0};
    ib_desc.usage.index_buffer = true;
    ib_desc.data = SG_RANGE(indices);
    ibuf = sg_make_buffer(&ib_desc);

    sg_shader_desc shd_desc = {0};
    shd_desc.attrs[0].glsl_name = "position";
    shd_desc.attrs[1].glsl_name = "texcoord0";
    shd_desc.vertex_func.source = "#version 330\n\
layout(location=0) in vec3 position;\n\
layout(location=1) in vec2 texcoord0;\n\
out vec4 color;\n\
out vec2 uv;\n\
void main() {\n\
  gl_Position = vec4(position, 1.0f);\n\
  uv = texcoord0;\n\
  color = vec4(uv, 0.0f, 1.0f);\n\
}\n";
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0] = (sg_shader_texture_sampler_pair){
        .stage = SG_SHADERSTAGE_FRAGMENT,
        .glsl_name = "tex",
        .view_slot = 0,
        .sampler_slot = 0,
    };
    shd_desc.fragment_func.source = "#version 330\n\
uniform sampler2D tex;\n\
in vec4 color;\n\
in vec2 uv;\n\
out vec4 frag_color;\n\
void main() {\n\
  frag_color = texture(tex, uv);\n\
  //frag_color = pow(frag_color, vec4(1.0f/2.2f));\n\
}\n";

    sg_image_desc img_desc = {0};
    img_desc.width = AGNES_SCREEN_WIDTH;
    img_desc.height = AGNES_SCREEN_HEIGHT;
    img_desc.label = "nes-texture";
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.usage.stream_update = true;
    img = sg_make_image(&img_desc);

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {0};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT32;
    pip = sg_make_pipeline(&pip_desc);

    bind.vertex_buffers[0] = vbuf;
    bind.index_buffer = ibuf;
    bind.views[0] = sg_make_view(&(sg_view_desc){ .texture.image = img });
    bind.samplers[0] = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });

    pass_action.colors[0] = (sg_color_attachment_action){
        .load_action = SG_LOADACTION_CLEAR,
        .store_action = SG_STOREACTION_STORE,
        .clear_value = {0.1, 0.2, 0.3, 1},
    };
}

agnes_color_t tmp_image[AGNES_SCREEN_WIDTH * AGNES_SCREEN_HEIGHT];
void frame() {
    const double dt = sapp_frame_duration();
    // processe input
    agnes_set_input(nes, &controller1, NULL);

    // step the NES state forward by 'dt' seconds, or more if in fast-forward
    agnes_next_frame(nes);

    // update tmp_image
    for (int y = 0; y < AGNES_SCREEN_HEIGHT; y++) {
        for (int x = 0; x < AGNES_SCREEN_WIDTH; x++) {
            tmp_image[y*AGNES_SCREEN_WIDTH+x] = agnes_get_screen_pixel(nes, x, y);
        }
    }

    sg_update_image(img, &(sg_image_data){
        .mip_levels[0] = (sg_range){ .ptr=&tmp_image, .size=(AGNES_SCREEN_WIDTH * AGNES_SCREEN_HEIGHT * sizeof(uint32_t)) }
    });

    sg_begin_pass(&(sg_pass){ .action = pass_action, .swapchain = sglue_swapchain(), .label = "main pass" });
    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);
    sg_draw(0, 6, 1);
    sg_end_pass();
    sg_commit();
}

void cleanup() {
    saudio_shutdown();
    sg_shutdown();
}

void input(const sapp_event* event) {
    switch (event->type) {
        case SAPP_EVENTTYPE_KEY_DOWN: {
            switch (event->key_code) {
                case SAPP_KEYCODE_Z:         controller1.a      = true; break;
                case SAPP_KEYCODE_X:         controller1.b      = true; break;
                case SAPP_KEYCODE_BACKSPACE: controller1.select = true; break;
                case SAPP_KEYCODE_ENTER:     controller1.start  = true; break;
                case SAPP_KEYCODE_UP:        controller1.up     = true; break;
                case SAPP_KEYCODE_DOWN:      controller1.down   = true; break;
                case SAPP_KEYCODE_LEFT:      controller1.left   = true; break;
                case SAPP_KEYCODE_RIGHT:     controller1.right  = true; break;
                default: break;
            }
            break;
        }
        case SAPP_EVENTTYPE_KEY_UP: {
            switch (event->key_code) {
                case SAPP_KEYCODE_Z:         controller1.a      = false; break;
                case SAPP_KEYCODE_X:         controller1.b      = false; break;
                case SAPP_KEYCODE_BACKSPACE: controller1.select = false; break;
                case SAPP_KEYCODE_ENTER:     controller1.start  = false; break;
                case SAPP_KEYCODE_UP:        controller1.up     = false; break;
                case SAPP_KEYCODE_DOWN:      controller1.down   = false; break;
                case SAPP_KEYCODE_LEFT:      controller1.left   = false; break;
                case SAPP_KEYCODE_RIGHT:     controller1.right  = false; break;
                default: break;
            }
            break;
        }
        default: break;
    }
}

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Please pass ROM path as first parameter.\n");
        return EXIT_FAILURE;
    }

    const char *ines_name = argv[1];

    size_t ines_data_size = 0;
    void* ines_data = read_file(ines_name, &ines_data_size);
    if (ines_data == NULL) {
        fprintf(stderr, "Reading %s failed.\n", ines_name);
        return EXIT_FAILURE;
    }

    nes = agnes_make();
    agnes_load_ines_data(nes, ines_data, ines_data_size);

    sapp_desc desc = {0};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup,
    desc.event_cb = input,
    desc .width = AGNES_SCREEN_WIDTH,
    desc.height = AGNES_SCREEN_HEIGHT,
    desc.window_title = "nesemu.cpp",
    desc.icon.sokol_default = true,
    desc.logger.func = slog_func;
    sapp_run(&desc);

    agnes_destroy(nes);
    free(ines_data);

    return 0;
}

static void audioCallback(float* buffer, int num_frames, int num_channels, void* user_data) {
    if (!nes) {
        // Fill with silence if NES is not initialized
        for (int i = 0; i < num_frames * num_channels; i++) {
            buffer[i] = 0.0f;
        }
        return;
    }
    
    // Fill the audio buffer with samples from the APU
    for (int i = 0; i < num_frames; i++) {
        float sample = agnes_get_audio_sample(nes);
        
        // Write to all channels (mono or stereo)
        for (int ch = 0; ch < num_channels; ch++) {
            buffer[i * num_channels + ch] = sample;
        }
    }
}

static void* read_file(const char *filename, size_t *out_len) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0L, SEEK_END);
    long pos = ftell(fp);
    if (pos < 0) {
        fclose(fp);
        return NULL;
    }
    size_t file_size = pos;
    rewind(fp);
    unsigned char *file_contents = (unsigned char *)malloc(file_size);
    if (!file_contents) {
        fclose(fp);
        return NULL;
    }
    if (fread(file_contents, file_size, 1, fp) < 1) {
        if (ferror(fp)) {
            fclose(fp);
            free(file_contents);
            return NULL;
        }
    }
    fclose(fp);
    *out_len = file_size;
    return file_contents;
}
