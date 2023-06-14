#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "bmflat.h"

#define DR_WAV_IMPLEMENTATION
#include "miniaudio/extras/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "miniaudio/extras/dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY
#include "miniaudio/extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#ifndef NO_FILE_DIALOG
#include "tinyfiledialogs/tinyfiledialogs.h"
#endif

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#ifdef CONSOLE
    #define GL2
    #define USE_RGBA
    #define LARGE_TEXT
    #define WIN_W   320
    #define WIN_H   240
    #define SAMPLE_FORMAT   ma_format_s16
    #define SAMPLE_TYPE     signed short
    #define SAMPLE_MAXVALSQ (32768 * 32768)
#else
    #define WIN_W   960
    #define WIN_H   540
    #define SAMPLE_FORMAT   ma_format_f32
    #define SAMPLE_TYPE     float
    #define SAMPLE_MAXVALSQ 1
#endif

#define TEX_W   96
#define TEX_H   48

#define ASPECT_RATIO    ((float)WIN_W / WIN_H)

// ffmpeg -f rawvideo -pix_fmt gray - -i
static unsigned char tex_data[TEX_H * TEX_W];

#define _MAX_VERTICES   16384
struct vertex {
    float x, y;
    float r, g, b, a;
    float tx, ty;
};
static struct vertex _vertices[_MAX_VERTICES];
static int _vertices_count;

static int flatspin_init();
static void flatspin_update(float dt);
static void flatspin_cleanup();

static const char *flatspin_bmspath;
static char *flatspin_basepath;

static inline void add_vertex_tex(
    float x, float y, float r, float g, float b, float a,
    float tx, float ty)
{
    if (_vertices_count >= _MAX_VERTICES) {
        fprintf(stderr, "> <  Too many vertices!");
        return;
    }
    _vertices[_vertices_count++] = (struct vertex){
        x, y, r, g, b, a, tx, ty
    };
}

static inline void add_vertex_a(float x, float y, float r, float g, float b, float a)
{
    add_vertex_tex(x, y, r, g, b, a, -1, -1);
}

static inline void add_vertex(float x, float y, float r, float g, float b)
{
    add_vertex_a(x, y, r, g, b, 1);
}

static inline void add_rect(
    float x, float y, float w, float h,
    float r, float g, float b, bool highlight)
{
    add_vertex(x, y + h, r, g, b);
    add_vertex(x, y, r, g, b);
    add_vertex(x + w, y, r, g, b);
    add_vertex(x + w, y, r, g, b);
    add_vertex(x + w, y + h,
        highlight ? (r * 0.7 + 0.3) : r,
        highlight ? (g * 0.7 + 0.3) : g,
        highlight ? (b * 0.7 + 0.3) : b);
    add_vertex(x, y + h, r, g, b);
}

static inline void add_rect_a(
    float x, float y, float w, float h,
    float r, float g, float b, float a)
{
    add_vertex_a(x, y + h, r, g, b, a);
    add_vertex_a(x, y, r, g, b, a);
    add_vertex_a(x + w, y, r, g, b, a);
    add_vertex_a(x + w, y, r, g, b, a);
    add_vertex_a(x + w, y + h, r, g, b, a);
    add_vertex_a(x, y + h, r, g, b, a);
}

static inline void add_rect_tex(
    float x, float y, float w, float h,
    float r, float g, float b, float a,
    float tx, float ty, float tw, float th)
{
    add_vertex_tex(x, y + h, r, g, b, a, tx, ty);
    add_vertex_tex(x, y, r, g, b, a, tx, ty + th);
    add_vertex_tex(x + w, y, r, g, b, a, tx + tw, ty + th);
    add_vertex_tex(x + w, y, r, g, b, a, tx + tw, ty + th);
    add_vertex_tex(x + w, y + h, r, g, b, a, tx + tw, ty);
    add_vertex_tex(x, y + h, r, g, b, a, tx, ty);
}

#ifdef LARGE_TEXT
#define TEXT_W  (1.0f / 15)
#else
#define TEXT_W  (1.0f / 30)
#endif

#define TEXT_H  (TEXT_W * 1.2f * ((float)WIN_W / WIN_H))

static inline void add_char(
    float x, float y, float r, float g, float b, float a, char ch)
{
    if (ch < 32 || ch > 127) ch = '?';
    int row = (ch - 32) / 16, col = ch % 16;
    add_rect_tex(x, y, TEXT_W, TEXT_H, r, g, b, a,
        col / 16.0f, row / 6.0f, 1 / 16.0f, 1 / 6.0f);
}

static inline void add_text(
    float x, float y, float r, float g, float b, float a, const char *s)
{
    for (; *s != '\0'; s++) {
        add_char(x, y, r, g, b, a, *s);
        x += TEXT_W;
    }
}

static inline int add_text_w(
    float x, float y, float w, float r, float g, float b, float a, const char *s)
{
    int lines = 1;
    float delta_x = 0;
    for (; *s != '\0'; s++) {
        add_char(x + delta_x, y, r, g, b, a, *s);
        delta_x += TEXT_W;
        if (delta_x + TEXT_W >= w) {
            delta_x = 0;
            lines++;
            y -= TEXT_H;
        }
    }
    return lines;
}

static inline GLuint load_shader(GLenum type, const char *source)
{
    GLuint shader_id = glCreateShader(type);
    glShaderSource(shader_id, 1, &source, NULL);
    glCompileShader(shader_id);

    GLint status;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &status);
    char msg_buf[1024];
    glGetShaderInfoLog(shader_id, sizeof(msg_buf) - 1, NULL, msg_buf);
    fprintf(stderr, "OvO  Compilation log for %s shader\n",
        (type == GL_VERTEX_SHADER ? "vertex" :
         type == GL_FRAGMENT_SHADER ? "fragment" : "unknown (!)"));
    fputs(msg_buf, stderr);
    fprintf(stderr, "=v=  End\n");
    if (status != GL_TRUE) {
        fprintf(stderr, "> <  Shader compilation failed\n");
        return 0;
    }

    return shader_id;
}

static GLFWwindow *window; 

static void audio_data_callback(
    ma_device *device, SAMPLE_TYPE *output, const SAMPLE_TYPE *input, ma_uint32 nframes);
static ma_device audio_device;

static void glfw_err_callback(int error, const char *desc)
{
    fprintf(stderr, "> <  GLFW: (%d) %s\n", error, desc);
}

static void glfw_fbsz_callback(GLFWwindow *window, int w, int h)
{
    glViewport(0, 0, w, h);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
#ifdef NO_FILE_DIALOG
        fprintf(stderr, "=~=  Usage: %s <path to BMS>\n", argv[0]);
        return 0;
#else
    #ifdef _WIN32
        FreeConsole();
    #endif

        // Open dialog
        const char *filters[] = { "*.bms", "*.bme", "*.bml", "*.pms" };
        const char *file = tinyfd_openFileDialog(
            NULL, NULL, 4, filters, "Be-Music Source", 0);
        if (file == NULL) return 0;
        flatspin_bmspath = file;
#endif
    } else {
        flatspin_bmspath = argv[1];
    }

    // Extract path and executable path
    int p = -1;
    for (int i = 0; flatspin_bmspath[i] != '\0'; i++)
        if (flatspin_bmspath[i] == '/' || flatspin_bmspath[i] == '\\') p = i;
    if (p == -1) {
        flatspin_basepath = "./";
    } else {
        flatspin_basepath = (char *)malloc(p + 2);
        memcpy(flatspin_basepath, flatspin_bmspath, p + 1);
        flatspin_basepath[p + 1] = '\0';
    }
    fprintf(stderr, "^ ^  Asset search path: %s\n", flatspin_basepath);

    // Initialize miniaudio
    ma_device_config dev_config =
        ma_device_config_init(ma_device_type_playback);
    dev_config.playback.format = SAMPLE_FORMAT;
    dev_config.playback.channels = 2;
    dev_config.sampleRate = 44100;
    dev_config.dataCallback = (ma_device_callback_proc)audio_data_callback;

    if (ma_device_init(NULL, &dev_config, &audio_device) != MA_SUCCESS ||
        (false && ma_device_start(&audio_device) != MA_SUCCESS))
    {
        fprintf(stderr, "> <  Cannot start audio playback");
        return 3;
    }

    // -- Initialization --

    glfwSetErrorCallback(glfw_err_callback);

    if (!glfwInit()) {
        fprintf(stderr, "> <  Cannot initialize GLFW\n");
        return 2;
    }

#ifdef GL2
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

    window = glfwCreateWindow(WIN_W, WIN_H, "flatspin", NULL, NULL);
    if (window == NULL) {
        fprintf(stderr, "> <  Cannot create GLFW window\n");
        return 2;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "> <  Cannot initialize GLEW\n");
        return 2;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // -- Resource allocation --
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

#ifdef GL2

#define GLSL(__source) "#version 120\n" #__source

    const char *vshader_source = GLSL(
        attribute vec2 ppp;
        attribute vec4 qwq;
        attribute vec2 uwu;
        varying vec4 qwq_frag;
        varying vec2 uwu_frag;
        void main()
        {
            gl_Position = vec4(ppp, 0.0, 1.0);
            qwq_frag = qwq;
            uwu_frag = uwu;
        }
    );

    const char *fshader_source = GLSL(
        varying vec4 qwq_frag;
        varying vec2 uwu_frag;
        uniform sampler2D tex;
        void main()
        {
            vec4 chroma = qwq_frag;
            if (uwu_frag.x >= -0.5f) {
                chroma.a *= texture2D(tex, uwu_frag).r;
            }
            gl_FragColor = chroma;
        }
    );

#else

#define GLSL(__source) "#version 150 core\n" #__source

    const char *vshader_source = GLSL(
        in vec2 ppp;
        in vec4 qwq;
        in vec2 uwu;
        out vec4 qwq_frag;
        out vec2 uwu_frag;
        void main()
        {
            gl_Position = vec4(ppp, 0.0, 1.0);
            qwq_frag = qwq;
            uwu_frag = uwu;
        }
    );

    const char *fshader_source = GLSL(
        in vec4 qwq_frag;
        in vec2 uwu_frag;
        uniform sampler2D tex;
        out vec4 ooo;
        void main()
        {
            if (uwu_frag.x < -0.5f) {
                ooo = qwq_frag;
            } else {
                ooo = vec4(
                    qwq_frag.r, qwq_frag.g, qwq_frag.b,
                    qwq_frag.a * texture(tex, uwu_frag));
            }
        }
    );

#endif

    GLuint vshader = load_shader(GL_VERTEX_SHADER, vshader_source);
    GLuint fshader = load_shader(GL_FRAGMENT_SHADER, fshader_source);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vshader);
    glAttachShader(prog, fshader);
#ifndef GL2
    glBindFragDataLocation(prog, 0, "ooo");
#endif
    glLinkProgram(prog);
    glUseProgram(prog);

    GLuint ppp_attrib_index = glGetAttribLocation(prog, "ppp");
    glEnableVertexAttribArray(ppp_attrib_index);
    glVertexAttribPointer(ppp_attrib_index, 2, GL_FLOAT, GL_FALSE,
        sizeof(struct vertex), (void *)offsetof(struct vertex, x));

    GLuint qwq_attrib_index = glGetAttribLocation(prog, "qwq");
    glEnableVertexAttribArray(qwq_attrib_index);
    glVertexAttribPointer(qwq_attrib_index, 4, GL_FLOAT, GL_FALSE,
        sizeof(struct vertex), (void *)offsetof(struct vertex, r));

    GLuint uwu_attrib_index = glGetAttribLocation(prog, "uwu");
    glEnableVertexAttribArray(uwu_attrib_index);
    glVertexAttribPointer(uwu_attrib_index, 2, GL_FLOAT, GL_FALSE,
        sizeof(struct vertex), (void *)offsetof(struct vertex, tx));

    for (int i = 0; i < TEX_W * TEX_H; i++) tex_data[i] = -tex_data[i];

    unsigned char *buf;
    GLenum format;

#ifdef USE_RGBA
    unsigned char _zz[TEX_W * TEX_H * 4];
    for (int i = 0; i < TEX_W * TEX_H; i++)
        _zz[i * 4] = _zz[i * 4 + 1] = _zz[i * 4 + 2] = _zz[i * 4 + 3] = tex_data[i];
    buf = _zz;
    format = GL_RGBA;
#else
    buf = tex_data;
    format = GL_RED;
#endif

    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, format, TEX_W, TEX_H,
        0, format, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);

    int result = flatspin_init();
    if (result != 0) return result;

    // -- Event/render loop --

    double updated_until = glfwGetTime();
    float step_dur = 1.0f / 120;

    glfwSetFramebufferSizeCallback(window, glfw_fbsz_callback);

    const int RECORD_W = WIN_W * 2;
    const int RECORD_H = WIN_H * 2;
    unsigned char *scr_buf = (unsigned char *)malloc(RECORD_W * RECORD_H * 3);
    int record_frame_num = 0;
    fprintf(stderr, "^ ^  Screen recording size is %dx%d\n", RECORD_W, RECORD_H);

    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;

        glClearColor(0.7f, 0.7f, 0.7f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        for (int i = 0; i < 2; i++) flatspin_update(step_dur);

        glBufferData(GL_ARRAY_BUFFER,
            _vertices_count * sizeof(struct vertex), _vertices, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, _vertices_count);

        // Read pixels
        glFlush();
        glReadPixels(
          0, 0, RECORD_W, RECORD_H, GL_RGB, GL_UNSIGNED_BYTE,
          scr_buf);
        /* if (record_frame_num == 0) {
          FILE *f = fopen("1.ppm", "w");
          fprintf(f, "P6\n%d %d\n255\n", RECORD_W, RECORD_H);
          for (int r = RECORD_H - 1; r >= 0; r--)
              for (int c = 0; c < RECORD_W * 3; c++)
                  fputc(scr_buf[r * RECORD_W * 3 + c], f);
          fclose(f);
        } */
        // F=$(mktemp -u -t bmflat)
        // mkfifo $F
        // flatspin >$F
        // ffmpeg -f rawvideo -pixel_format rgb24 -video_size 1920x1080 -framerate 60 -t 146 -i $F -vf "scale=800x450" -pix_fmt yuv420p -crf 25 output.mp4
        // ffmpeg -i bmflat.mp4 -i epilogue.wav -t 4 -f lavfi -i anullsrc=channel_layout=stereo:sample_rate=44100 -filter_complex "[2:a][1:a]concat=n=2:v=0:a=1" -c:v copy -b:a 80k bmflat-demo.mp4
        for (int r = RECORD_H - 1; r >= 0; r--)
            fwrite(scr_buf + (r * RECORD_W * 3), RECORD_W * 3, 1, stdout);
        fflush(stdout);
        record_frame_num++;

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteProgram(prog);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    glDeleteTextures(1, &tex);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);

    glfwTerminate();
#ifdef CONSOLE
    ma_device_uninit(&audio_device);

    if (p == -1) free(flatspin_basepath);
    flatspin_cleanup();
#endif

    return 0;
}

// -- Application logic --

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;

    char *buf = NULL;

    do {
        if (fseek(f, 0, SEEK_END) != 0) break;
        long len = ftell(f);
        if (fseek(f, 0, SEEK_SET) != 0) break;
        if ((buf = (char *)malloc(len + 1)) == NULL) break;
        if (fread(buf, len, 1, f) != 1) { free(buf); buf = NULL; break; }
        buf[len] = 0;
    } while (0);

    fclose(f);
    return buf;
}

static int msgs_count;
static struct bm_chart chart;
static struct bm_seq seq;

static bool is_bms_sp;
static bool is_9k;

#define MSGS_FADE_OUT_TIME  0.2
static float msgs_show_time = -MSGS_FADE_OUT_TIME;

static bool show_stats = false;
static int fps_accum = 0, fps_record = 0;
static float fps_record_time = 0;

static bool pcm_loaded = false;
// 0 - not loaded
// 1 - loaded
// 2 - failed
static int pcm_load_state[BM_INDEX_MAX] = { 0 };
static int wave_count = 0;

static SAMPLE_TYPE *pcm[BM_INDEX_MAX] = { NULL };
static ma_uint64 pcm_len[BM_INDEX_MAX] = { 0 };
#define GAIN    0.5

static int pcm_track[BM_INDEX_MAX];
static ma_uint64 pcm_pos[BM_INDEX_MAX];

// These are updatd by the display deterministically,
// independent from the audio thread
static int pcm_track_disp[BM_INDEX_MAX];
static ma_uint64 pcm_pos_disp[BM_INDEX_MAX];

#define TOTAL_TRACKS    (8 + BM_BGM_TRACKS)
#define RMS_WINDOW_SIZE 5
static float msq_gframe[TOTAL_TRACKS][RMS_WINDOW_SIZE] = {{ 0 }};
static int msq_ptr[TOTAL_TRACKS] = { 0 };
static float msq_sum[TOTAL_TRACKS] = { 0 };

static int msq_accum_size = 0;
static float msq_accum[TOTAL_TRACKS] = { 0 };

#define SCRATCH_WIDTH   4
#define KEY_WIDTH       3
#define BGTRACK_WIDTH   2

#define HITLINE_POS     -0.2f
#define HITLINE_H       0.01

static float unit;

static float play_pos;
static float scroll_speed;
static float fwd_range;
static float bwd_range;
#define Y_POS(__pos)    (((__pos) - play_pos) * scroll_speed + HITLINE_POS)

static bool playing = false;
static float current_bpm;   // Will be re-initialized on playback start
static int event_ptr;

static float ss_target;
#define SS_MIN  (0.1f / 48)
#define SS_MAX  (1.2f / 48)
#define SS_DELTA    (0.05f / 48)
#define SS_INITIAL  (0.6f / 48)

static void audio_data_callback(
    ma_device *device, SAMPLE_TYPE *output, const SAMPLE_TYPE *input, ma_uint32 nframes)
{
    ma_mutex_lock(&device->lock);

    ma_zero_pcm_frames(output, nframes, SAMPLE_FORMAT, 2);
    if (pcm_loaded) for (int i = 0; i < BM_INDEX_MAX; i++) {
        int track = pcm_track[i];
        if (track != -1) {
            int start = pcm_pos[i];
            int j;
            for (j = 0; j < nframes && start + j < pcm_len[i]; j++) {
                SAMPLE_TYPE lsmp = pcm[i][(start + j) * 2];
                SAMPLE_TYPE rsmp = pcm[i][(start + j) * 2 + 1];
                output[j * 2] += lsmp * GAIN;
                output[j * 2 + 1] += rsmp * GAIN;
            }
            pcm_pos[i] += j;
            if (pcm_pos[i] >= pcm_len[i]) pcm_track[i] = -1;
        }
    }

    ma_mutex_unlock(&device->lock);

    (void)input;    // Unused
}

static inline void delta_ss_step(float dt)
{
    float dist = ss_target - scroll_speed;
    if (dist == 0) return;
    float delta = dist * dt * 2.5f;
    if (fabs(delta) < 1e-6) delta = (dist > 0 ? 1e-6 : -1e-6);
    if (fabs(delta) >= fabs(dist)) {
      scroll_speed = ss_target;
    } else {
      scroll_speed += delta;
    }
    fwd_range = (1.1 - HITLINE_POS) / scroll_speed;
    bwd_range = (HITLINE_POS + 1.1) / scroll_speed;
}

static inline void delta_ss_submit(float delta)
{
    ss_target += delta;
    if (ss_target < SS_MIN) ss_target = SS_MIN;
    if (ss_target > SS_MAX) ss_target = SS_MAX;
}

#define PARTICLE_SIZE   0.0035
#define PARTICLE_LIFE   0.5
#define PARTICLES_MAX   1024
#define GLOW_LIFE       0.75
#define GLOWS_MAX       64

static int particle_count = 0;
static struct particle {
    float x, y, r, g, b;
    float vx, vy;
    float t, life;
} particles[PARTICLES_MAX];

static int glow_count = 0;
static struct glow {
    float x, w, t;
} glows[GLOWS_MAX];

static inline void add_particle(float T, float x, float y, float r, float g, float b)
{
    if (particle_count >= PARTICLES_MAX) return;
    float a = (float)rand() / RAND_MAX * M_PI * 2;
    float t = ((float)rand() / RAND_MAX * 0.2 + 0.9) * PARTICLE_LIFE;
    float l = ((float)rand() / RAND_MAX * 0.4 + 0.8);
    particles[particle_count++] = (struct particle) {
        x, y, r, g, b, 0.02 * l * cos(a), 0.06 * l * sin(a), T, t
    };
}

static inline void add_glow(float T, float x, float w)
{
    if (glow_count >= GLOWS_MAX) return;
    glows[glow_count++] = (struct glow) { x, w, T };
}

static inline void add_particles_on_line(float T, float x, float w, float r, float g, float b)
{
    int number = (int)(w / 0.01);
    for (int i = 0; i < number; i++) {
        float dx = (float)rand() / RAND_MAX * w;
        add_particle(T, x + dx, HITLINE_POS, r, g, b);
    }
    add_glow(T, x, w);
}

static inline void update_and_draw_particles(float T)
{
    for (int i = 0; i < particle_count; i++) {
        if (T - particles[i].t >= particles[i].life) {
            // To be removed
            particles[i] = particles[--particle_count];
            i--;
        } else {
            float r0 = (T - particles[i].t) / particles[i].life;
            float r = 1 - expf(-r0 * 5);
            add_rect_a(
                particles[i].x + particles[i].vx * r,
                particles[i].y + particles[i].vy * r,
                PARTICLE_SIZE, PARTICLE_SIZE * ASPECT_RATIO,
                particles[i].r, particles[i].g, particles[i].b,
                1 - r0);
        }
    }

    for (int i = 0; i < glow_count; i++) {
        if (T - glows[i].t >= GLOW_LIFE) {
            glows[i] = glows[--glow_count];
            i--;
        } else {
            float r0 = (T - glows[i].t) / GLOW_LIFE;
            float r = expf(-r0 * 5);
            add_rect_a(
                glows[i].x, HITLINE_POS - 0.005 * r,
                glows[i].w, HITLINE_H + 0.01 * r,
                1.0f, 0.85f, 0.7f, r);
        }
    }
}

static const char *base36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static inline ma_result try_load_audio(
    const char *path, ma_decoder_config *cfg, ma_uint64 *len, SAMPLE_TYPE **ptr)
{
    ma_result result = ma_decode_file(path, cfg, len, (void **)ptr);
    if (result == MA_SUCCESS) return result;

    // Try other extensions first
    int p = -1, q;
    for (q = 0; path[q] != '\0'; q++) if (path[q] == '.') p = q;
    if (p == -1) p = q;
    char *new_path = (char *)malloc(p + 5);
    memcpy(new_path, path, p + 1);
    new_path[p + 4] = '\0';

    const char *extensions[] = { "wav", "ogg", "mp3" };
    for (int i = 0; i < sizeof extensions / sizeof extensions[0]; i++) {
        memcpy(new_path + p + 1, extensions[i], 3);
        ma_result inner_result = ma_decode_file(new_path, cfg, len, (void **)ptr);
        if (inner_result == MA_SUCCESS) {
            free(new_path);
            return inner_result;
        }
    }

    free(new_path);
    return result;
}

ma_thread audio_load_thread;

static ma_thread_result MA_THREADCALL flatspin_load_audio(void *data)
{
    // Load PCM data
    ma_decoder_config dec_config = ma_decoder_config_init(SAMPLE_FORMAT, 2, 44100);
    char s[1024] = { 0 };
    strcpy(s, flatspin_basepath);
    int len = strlen(flatspin_basepath);
    for (int i = 0; i < BM_INDEX_MAX; i++) if (chart.tables.wav[i] != NULL) {
        strncpy(s + len, chart.tables.wav[i], sizeof(s) - len - 1);
        SAMPLE_TYPE *ptr;
        ma_uint64 len;
        ma_result result = try_load_audio(s, &dec_config, &len, &ptr);
        ma_mutex_lock(&audio_device.lock);
        if (result != MA_SUCCESS) {
            pcm_len[i] = 0;
            pcm[i] = NULL;
            pcm_load_state[i] = 2;
        } else {
            pcm_len[i] = len;
            pcm[i] = ptr;
            pcm_load_state[i] = 1;
        }
        ma_mutex_unlock(&audio_device.lock);
    }

    for (int i = 0; i < BM_INDEX_MAX; i++) {
        pcm_track[i] = pcm_track_disp[i] = -1;
        pcm_pos[i] = pcm_pos_disp[i] = 0;
    }

    ma_mutex_lock(&audio_device.lock);
    pcm_loaded = true;
    ma_mutex_unlock(&audio_device.lock);

    return (ma_thread_result)0;
}

static int flatspin_init()
{
    char *src = read_file(flatspin_bmspath);
    if (src == NULL) {
        fprintf(stderr, "> <  Cannot load BMS file %s\n", flatspin_bmspath);
        return 1;
    }

    msgs_count = bm_load(&chart, src);
    free(src);

    is_bms_sp = (chart.meta.player_num == 1);
    is_9k = (chart.meta.player_num == 3);
    if (!is_bms_sp && !is_9k) is_bms_sp = true;

    bm_to_seq(&chart, &seq);
    msgs_show_time = 10;

    unit = 2.0f / (
        (is_bms_sp ? (SCRATCH_WIDTH + KEY_WIDTH * 7) : KEY_WIDTH * 9) +
        BGTRACK_WIDTH * chart.tracks.background_count);

    play_pos = 0;
    scroll_speed = SS_INITIAL;  // Screen Y units per 1/48 beat
    fwd_range = (1.1 - HITLINE_POS) / scroll_speed;
    bwd_range = (HITLINE_POS + 1.1) / scroll_speed;

    ss_target = SS_INITIAL;

    srand(0);

    wave_count = 0;
    for (int i = 0; i < BM_INDEX_MAX; i++)
        wave_count += (chart.tables.wav[i] != NULL);

    ma_result result = ma_thread_create(
        audio_device.pContext, &audio_load_thread,
        flatspin_load_audio, NULL);
    if (result != MA_SUCCESS) {
        // Fall back to synchronous loading
        flatspin_load_audio(NULL);
    }

    return 0;
}

static inline int track_index(int id)
{
    if (is_bms_sp) {
        if (id == 16)
            return 0;
        else if (id >= 11 && id <= 19 && id != 17)
            return (id < 17 ? id - 10 : id - 12);
        else if (id <= 0)
            return 8 - id;
        else return -1;
    } else if (is_9k) {
        if (id >= 11 && id <= 15) return id - 11;
        else if (id >= 22 && id <= 25) return id - 17;
        else if (id <= 0) return 9 - id;
        else return -1;
    }
    return -1;  // Unreachable
}

static inline void track_attr(
    int id, float *x, float *w, float *r, float *g, float *b)
{
    if (is_bms_sp) {
        if (id == 16) {
            *x = -1.0f;
            *w = unit * SCRATCH_WIDTH;
            *r = 1.0f;
            *g = 0.4f;
            *b = 0.3f;
        } else if (id >= 11 && id <= 19 && id != 17) {
            int i = (id < 17 ? id - 11 : id - 13);
            *x = -1.0f + unit * (SCRATCH_WIDTH + KEY_WIDTH * i);
            *w = unit * KEY_WIDTH;
            *r = i % 2 == 0 ? 0.85f : 0.5f;
            *g = i % 2 == 0 ? 0.85f : 0.5f;
            *b = i % 2 == 0 ? 0.85f : 1.0f;
        } else if (id <= 0) {
            int i = -id;
            *x = -1.0f + unit * (SCRATCH_WIDTH + KEY_WIDTH * 7 + BGTRACK_WIDTH * i);
            *w = unit * BGTRACK_WIDTH;
            *r = i % 2 == 0 ? 1.0f : 0.6f;
            *g = i % 2 == 0 ? 0.9f : 0.8f;
            *b = i % 2 == 0 ? 0.6f : 0.5f;
        }
    } else if (is_9k) {
        if ((id >= 11 && id <= 15) ||
            (id >= 22 && id <= 25))
        {
            int i = (id <= 15 ? id - 11 : id - 17);
            *x = -1.0f + unit * KEY_WIDTH * i;
            *w = unit * KEY_WIDTH;
            static const float track_colours[5][3] = {
                {0.85f, 0.85f, 0.85f},
                //{0.9f, 0.95f, 0.4f},
                {1.0f, 0.9f, 0.6f},
                //{0.4f, 0.8f, 0.5f},
                {0.6f, 0.8f, 0.5f},
                {0.5f, 0.5f, 1.0f},
                {1.0f, 0.6f, 0.5f}
            };
            int j = (i <= 4 ? i : 8 - i);
            *r = track_colours[j][0];
            *g = track_colours[j][1];
            *b = track_colours[j][2];
        } else if (id <= 0) {
            int i = -id;
            *x = -1.0f + unit * (KEY_WIDTH * 9 + BGTRACK_WIDTH * i);
            *w = unit * BGTRACK_WIDTH;
            *r = i % 2 == 0 ? 0.8f : 0.7f;
            *g = i % 2 == 0 ? 0.6f : 0.8f;
            *b = i % 2 == 0 ? 0.8f : 0.3f;
        }
    }
}

static inline void draw_track_background(int id)
{
    float x, w, r, g, b;
    track_attr(id, &x, &w, &r, &g, &b);
    float rms = sqrtf(msq_sum[track_index(id)] / RMS_WINDOW_SIZE);
    float l = 0.15 + 0.75 * sqrtf(rms);
    add_rect(x, -1, w, 2, r * l, g * l, b * l, false);
}

static void flatspin_update(float dt)
{
    // -- Events --

    bool play_started = false;
    bool play_cut = false;
    bool moved = false;

    static int keys_prev[8] = { GLFW_RELEASE }; // GLFW_RELEASE == 0
    int keys[8] = {
        glfwGetKey(window, GLFW_KEY_UP),
        glfwGetKey(window, GLFW_KEY_DOWN),
        glfwGetKey(window, GLFW_KEY_LEFT),
        glfwGetKey(window, GLFW_KEY_RIGHT),
        glfwGetKey(window, GLFW_KEY_SPACE),
        glfwGetKey(window, GLFW_KEY_ENTER),
        glfwGetKey(window, GLFW_KEY_TAB),
        glfwGetKey(window, GLFW_KEY_U)
    };

    if (keys[2] == GLFW_PRESS && keys_prev[2] == GLFW_RELEASE) {
        // Left: scroll-
        delta_ss_submit(-SS_DELTA);
    } else if (keys[3] == GLFW_PRESS && keys_prev[3] == GLFW_RELEASE) {
        // Right: scroll+
        delta_ss_submit(+SS_DELTA);
    }

    int mul =
        (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
         glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) ? 4 : 1;
    if (keys[0] == GLFW_PRESS && keys[1] == GLFW_RELEASE) {
        // Up: play pos+
        play_pos += dt * 288 / (scroll_speed / SS_INITIAL) * mul;
        playing = false;
        play_cut = true;
        moved = true;
    } else if (keys[1] == GLFW_PRESS && keys[0] == GLFW_RELEASE) {
        // Down: play pos-
        play_pos -= dt * 288 / (scroll_speed / SS_INITIAL) * mul;
        playing = false;
        play_cut = true;
        moved = true;
    }

    if (keys[4] == GLFW_PRESS && keys_prev[4] == GLFW_RELEASE) {
        // Space: start/stop
        play_cut = playing;
        play_started = playing = !playing; 
    }

    if (keys[5] == GLFW_PRESS && keys_prev[5] == GLFW_RELEASE) {
        // Enter: restart/stop
        play_cut = playing;
        if (!playing) play_pos = 0;
        play_started = playing = !playing;
    }

    static float autoplay_time = 0;
    float last_autoplay_time = autoplay_time;
    autoplay_time += dt;
    if (last_autoplay_time < 4 && autoplay_time >= 4) {
        play_cut = playing;
        play_started = playing = true;
    }

    if (play_started) {
        // Current BPM needs to be updated
        // BGA needs an update as well, but our application doesn't display BGAs
        // XXX: Can be replaced with binary search
        current_bpm = chart.meta.init_tempo;
        int i;
        for (i = 0; i < seq.event_count; i++) {
            struct bm_event ev = seq.events[i];
            if (ev.pos >= play_pos) break;
            if (ev.type == BM_TEMPO_CHANGE) current_bpm = ev.value_f;
        }
        event_ptr = i;
    }
    if (play_cut || play_started) {
        // Stop all sounds
        ma_mutex_lock(&audio_device.lock);
        for (int i = 0; i < BM_INDEX_MAX; i++)
            pcm_track[i] = pcm_track_disp[i] = -1;
        ma_mutex_unlock(&audio_device.lock);
    }

    show_stats ^= (keys[6] == GLFW_PRESS && keys_prev[6] == GLFW_RELEASE);
    show_stats ^= (keys[7] == GLFW_PRESS && keys_prev[7] == GLFW_RELEASE);

    memcpy(keys_prev, keys, sizeof keys);

    // -- Updates --

    static float T = 0;
    T += dt;

    delta_ss_step(dt);

    // Update RMS display
    // These do not need the lock since no concurrent access may happen
    // (`pcm` and `pcm_len` are not modified after `pcm_loaded` is set
    static float frac_samples = 0;
    frac_samples += dt * 44100;
    int nframes = (int)frac_samples;
    frac_samples -= nframes;
    if (pcm_loaded) {
        // XXX: Reduce duplication?
        for (int i = 0; i < BM_INDEX_MAX; i++) {
            int track = pcm_track_disp[i];
            if (track != -1) {
                int start = pcm_pos_disp[i];
                int j;
                for (j = 0; j < nframes && start + j < pcm_len[i]; j++) {
                    SAMPLE_TYPE lsmp = pcm[i][(start + j) * 2];
                    SAMPLE_TYPE rsmp = pcm[i][(start + j) * 2 + 1];
                    msq_accum[track] += (float)lsmp * lsmp + (float)rsmp * rsmp;
                }
                pcm_pos_disp[i] += j;
                if (pcm_pos_disp[i] >= pcm_len[i]) pcm_track_disp[i] = -1;
            }
        }
        msq_accum_size += nframes;
    }

    if (pcm_loaded) {
        // Fade out log messages on any movement
        if ((moved || play_started) && msgs_show_time > 0) msgs_show_time = 0;
        if (msgs_show_time > -MSGS_FADE_OUT_TIME) msgs_show_time -= dt;
    }

    ma_mutex_lock(&audio_device.lock);

    if (playing) {
        play_pos += dt * current_bpm * (48.0f / 60.0f);

        float x, w, r, g, b;

        while (event_ptr < seq.event_count && seq.events[event_ptr].pos <= play_pos) {
            struct bm_event ev = seq.events[event_ptr];
            switch (ev.type) {
            case BM_TEMPO_CHANGE:
                current_bpm = ev.value_f;
                break;
            case BM_NOTE:
            case BM_NOTE_LONG:
                pcm_track[ev.value] = pcm_track_disp[ev.value] =
                  track_index(ev.track);
                pcm_pos[ev.value] = pcm_pos_disp[ev.value] = 0;
                // Create particles
                track_attr(ev.track, &x, &w, &r, &g, &b);
                add_particles_on_line(T, x, w, r, g, b);
                break;
            case BM_NOTE_OFF:
                if (ev.track == 14 && ev.pos == 17792)
                    ss_target = SS_MIN;
                break;
            case BM_BARLINE:
                if (ev.value == 8) {
                    ss_target = SS_INITIAL - SS_DELTA * 4;
                } else if (ev.value == 88) {
                    ss_target = SS_INITIAL - SS_DELTA * 2;
                }
                break;
            default: break;
            }
            event_ptr++;
        }
    }

    ma_mutex_unlock(&audio_device.lock);

    if (play_pos < 0) {
        play_pos = 0;
    } else if (play_pos > seq.events[seq.event_count - 1].pos) {
        if (playing) {
            add_particles_on_line(T, -1, 2, 0.6, 0.7, 0.4);
            add_particles_on_line(T, -1, 2, 0.6, 0.9, 0.4);
            add_particles_on_line(T, -1, 2, 0.5, 1.0, 0.4);
        }
        play_pos = seq.events[seq.event_count - 1].pos;
        playing = false;
    }

    // Audio RMS data

    if (msq_accum_size != 0) {
        #define process_track(__i) do { \
            int index = track_index(__i); \
            float z = msq_accum[index] / msq_accum_size / SAMPLE_MAXVALSQ; \
            msq_sum[index] -= msq_gframe[index][msq_ptr[index]]; \
            msq_gframe[index][msq_ptr[index]] = z; \
            msq_sum[index] += z; \
            /* Floating point errors may occur */ \
            if (msq_sum[index] < 1e-4) msq_sum[index] = 0; \
            msq_ptr[index] = (msq_ptr[index] + 1) % RMS_WINDOW_SIZE; \
            msq_accum[index] = 0; \
        } while (0)

        if (is_bms_sp) {
            for (int i = 11; i <= 19; i++)
                if (i != 17) process_track(i);
        } else if (is_9k) {
            for (int i = 11; i <= 15; i++) process_track(i);
            for (int i = 22; i <= 25; i++) process_track(i);
        }
        for (int i = 0; i < chart.tracks.background_count; i++)
            process_track(-i);

        msq_accum_size = 0;
    }

    // -- Drawing --

    _vertices_count = 0;

    if (is_bms_sp) {
        for (int i = 11; i <= 19; i++)
            if (i != 17) draw_track_background(i);
    } else if (is_9k) {
        for (int i = 11; i <= 15; i++) draw_track_background(i);
        for (int i = 22; i <= 25; i++) draw_track_background(i);
    }
    for (int i = 0; i < chart.tracks.background_count; i++)
        draw_track_background(-i);

    int start = 0;
    int lo = -1, hi = seq.event_count, mid;
    while (lo < hi - 1) {
        mid = (lo + hi) >> 1;
        if (seq.events[mid].pos < play_pos - bwd_range) lo = mid;
        else hi = mid;
    }
    start = hi;

    char s[12];
    for (int i = start; i < seq.event_count && seq.events[i].pos <= play_pos + fwd_range; i++) {
        float bpm = -1;
        if (seq.events[i].type == BM_BARLINE) {
            if (seq.events[i].pos == seq.events[seq.event_count - 1].pos) {
                add_rect(-1, Y_POS(seq.events[i].pos), 2, 0.01, 0.6, 0.7, 0.4, false);
                add_text(1 - TEXT_W * 11.5, Y_POS(seq.events[i].pos) + TEXT_H / 8,
                    0.6, 0.7, 0.4, 1.0, "Fin \\(^ ^)/");
            } else {
                // For bar #000, this line will be covered by
                // the BPM change line, hence no need to change colour
                add_rect(-1, Y_POS(seq.events[i].pos), 2, 0.01, 0.4, 0.4, 0.4, false);
                sprintf(s, "#%03d", seq.events[i].value);
                add_text(1 - TEXT_W * 4.5, Y_POS(seq.events[i].pos) + TEXT_H / 8,
                    i == 0 ? 0.7 : 0.4,
                    i == 0 ? 0.6 : 0.4,
                    0.4,
                    1.0, s);
                if (i == 0) bpm = chart.meta.init_tempo;
            }
        }
        if (seq.events[i].type == BM_TEMPO_CHANGE)
            bpm = seq.events[i].value_f;
        if (bpm != -1) {
            add_rect(-1, Y_POS(seq.events[i].pos), 2, 0.01, 0.5, 0.5, 0.4, false);
            sprintf(s, "BPM %06.2f", bpm);
            add_text(1 - TEXT_W * 10.5, Y_POS(seq.events[i].pos) - TEXT_H * 9 / 8,
                0.5, 0.5, 0.4, 1.0, s);
        }
    }

    for (int i = start; i < seq.event_count && seq.events[i].pos <= play_pos + fwd_range; i++) {
        struct bm_event ev = seq.events[i];
        if (ev.type == BM_NOTE) {
            float x, w, r, g, b;
            track_attr(ev.track, &x, &w, &r, &g, &b);
            add_rect(x, Y_POS(ev.pos), w,
                0.02f,
                r, g, b, true);
        }
    }

    for (int i = 0; i < seq.long_note_count; i++) {
        struct bm_event ev = seq.long_notes[i];
        if (ev.pos <= play_pos + fwd_range &&
            ev.pos + ev.value_a >= play_pos - bwd_range)
        {
            float x, w, r, g, b;
            track_attr(ev.track, &x, &w, &r, &g, &b);
            add_rect(x, Y_POS(ev.pos), w,
                0.02f + ev.value_a * scroll_speed,
                r, g, b, true);
        }
    }

    // Hit line
    add_rect(-1, HITLINE_POS, 2, HITLINE_H, 1.0, 0.7, 0.4, false);
    update_and_draw_particles(T);

    // Messages from the parser
    if (msgs_show_time > -MSGS_FADE_OUT_TIME) {
        const int MAX_LOGS = 5;
        const int MAX_FAILURES = 5;

        char s[128];
        float y = 0.95 - TEXT_H * 1.75;
        float line_w = 1.9 - TEXT_W * 5;
        float alpha = (msgs_show_time > 0 ? 1 : 1 + msgs_show_time / MSGS_FADE_OUT_TIME);
        int disp_count = (msgs_count <= MAX_LOGS + 1 ? msgs_count : MAX_LOGS);
        for (int i = 0; i < disp_count; i++) {
            if (bm_logs[i].line != -1) {
                snprintf(s, sizeof s, "L%3d", bm_logs[i].line);
                add_text(-0.95, y, 1.0, 1.0, 0.7, alpha, s);
            } else {
                add_char(-0.95 + TEXT_W * 3, y, 1.0, 1.0, 0.7, alpha, '>');
            }
            int lines = add_text_w(-0.95 + TEXT_W * 5, y,
                line_w, 0.95, 0.95, 0.9, alpha, bm_logs[i].message);
            y -= TEXT_H * (lines + 0.75);
        }
        if (msgs_count > disp_count) {
            add_char(-0.95 + TEXT_W * 1, y, 1.0, 1.0, 0.7, alpha, '@');
            add_char(-0.95 + TEXT_W * 2, y, 1.0, 1.0, 0.7, alpha, ' ');
            add_char(-0.95 + TEXT_W * 3, y, 1.0, 1.0, 0.7, alpha, '@');
            sprintf(s, "... and %d more warnings", msgs_count - disp_count);
            int lines = add_text_w(-0.95 + TEXT_W * 5, y,
                line_w, 0.95, 0.95, 0.9, alpha, s);
            y -= TEXT_H * (lines + 0.75);
        }

        ma_mutex_lock(&audio_device.lock);
        int loaded_count = 0, failed_count = 0;
        for (int i = 0; i < BM_INDEX_MAX; i++) if (pcm_load_state[i] != 0) {
            loaded_count++;
            if (pcm_load_state[i] == 2) {
                if (++failed_count <= MAX_FAILURES) {
                    add_char(-0.95 + TEXT_W * 3, y, 1.0, 0.7, 0.7, alpha, '!');
                    snprintf(s, sizeof s, "Cannot load wave #%c%c [%s]",
                        base36[i / 36], base36[i % 36], chart.tables.wav[i]);
                    int lines = add_text_w(-0.95 + TEXT_W * 5, y,
                        line_w, 0.95, 0.9, 0.9, alpha, s);
                    y -= TEXT_H * (lines + 0.75);
                }
            }
        }
        if (failed_count > MAX_FAILURES) {
            add_char(-0.95 + TEXT_W * 1, y, 1.0, 0.7, 0.7, alpha, '>');
            add_char(-0.95 + TEXT_W * 2, y, 1.0, 0.7, 0.7, alpha, ' ');
            add_char(-0.95 + TEXT_W * 3, y, 1.0, 0.7, 0.7, alpha, '<');
            sprintf(s, "... and %d more audio file%s",
                failed_count - MAX_FAILURES,
                failed_count - MAX_FAILURES > 1 ? "s" : "");
            int lines = add_text_w(-0.95 + TEXT_W * 5, y,
                line_w, 0.95, 0.9, 0.9, alpha, s);
            y -= TEXT_H * (lines + 0.75);
        }
        if (pcm_loaded) {
            add_char(-0.95 + TEXT_W * 3, y, 0.8, 1.0, 0.7, alpha, '~');
            snprintf(s, sizeof s, "%s - %s", chart.meta.title, chart.meta.artist);
            add_text_w(-0.95 + TEXT_W * 5, y,
                line_w, 0.9, 0.95, 0.9, alpha, s);
        } else {
            add_char(-0.95 + TEXT_W * 1, y, 1.0, 0.9, 0.6, alpha, '.');
            add_char(-0.95 + TEXT_W * 2, y, 1.0, 0.9, 0.6, alpha, '.');
            add_char(-0.95 + TEXT_W * 3, y, 1.0, 0.9, 0.6, alpha, '.');
            snprintf(s, sizeof s, "Loading audio %4d/%4d", loaded_count, wave_count);
            int lines = add_text_w(-0.95 + TEXT_W * 5, y,
                line_w, 1.0, 0.95, 0.9, alpha, s);
            y -= TEXT_H * (lines + 0.75);
            if (playing) {
                add_char(-0.95 + TEXT_W * 1, y, 1.0, 0.9, 0.6, alpha, '=');
                add_char(-0.95 + TEXT_W * 2, y, 1.0, 0.9, 0.6, alpha, '~');
                add_char(-0.95 + TEXT_W * 3, y, 1.0, 0.9, 0.6, alpha, '=');
                add_text_w(-0.95 + TEXT_W * 5, y,
                    line_w, 1.0, 0.95, 0.9, alpha,
                    "No sounds right now, but trying very hard!");
            }
        }
        ma_mutex_unlock(&audio_device.lock);
    }

    if (show_stats) {
        fps_accum++;
        if ((fps_record_time += dt) >= 1) {
            fps_record_time -= 1;
            fps_record = fps_accum;
            fps_accum = 0;
        }
        int n_verts = _vertices_count;
        char s[32];
        snprintf(s, sizeof s, "%6d vertices", n_verts);
        add_text(0.95 - TEXT_W * 15, -1 + TEXT_H * 2, 1.0, 1.0, 1.0, 0.75, s);
        snprintf(s, sizeof s, "%3d ms | %2d FPS", (int)(dt * 1000 + 0.5), fps_record);
        add_text(0.95 - TEXT_W * 15, -1 + TEXT_H * 0.5, 1.0, 1.0, 1.0, 0.75, s);
    }
}

static void flatspin_cleanup()
{
    for (int i = 0; i < BM_INDEX_MAX; i++)
        if (pcm[i] != NULL) ma_free(pcm[i]);

    bm_close_chart(&chart);
    bm_close_seq(&seq);
}

// ffmpeg -f rawvideo -pix_fmt gray - -i font.png | hexdump -ve '1/1 "%.2x"' | fold -w96 | sed -e 's/00/0,/g' | sed -e 's/ff/1,/g'
static unsigned char tex_data[TEX_H * TEX_W] = {
    0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,1,0,0,0,1,1,0,0,0,0,0,1,1,0,0,0,0,0,1,0,0,0,
    0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,1,0,0,1,1,1,1,1,0,1,1,1,1,1,0,1,1,0,0,1,0,1,0,0,1,0,0,0,0,1,0,0,0,
    0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,1,0,1,0,0,0,0,0,0,1,0,0,1,0,0,1,0,0,0,0,0,0,0,0,
    0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,
    0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,1,1,1,1,1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,
    0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,1,0,0,
    0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,1,0,1,0,0,1,0,0,0,0,0,1,1,0,1,0,0,0,0,0,0,0,
    0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,1,1,1,1,1,0,1,0,0,1,1,0,1,0,0,1,0,0,0,0,0,0,0,0,
    0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
    0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,1,0,0,0,0,0,0,1,1,0,0,1,1,0,1,0,0,0,0,0,0,0,
    0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,1,1,0,0,0,0,1,0,0,0,1,1,1,1,0,0,0,1,1,1,0,0,1,0,0,0,1,0,1,1,1,1,1,0,0,1,1,1,0,0,1,1,1,1,1,0,
    0,1,1,1,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
    1,0,0,0,1,0,0,1,1,0,0,0,0,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,
    1,0,0,0,1,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,
    1,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,
    1,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,0,0,0,0,0,0,1,0,
    1,0,1,0,1,0,0,0,1,0,0,0,0,1,1,1,0,0,0,0,1,1,0,0,1,1,1,1,1,0,1,1,1,1,0,0,1,1,1,1,0,0,0,0,0,1,0,0,
    0,1,1,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,
    1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,1,1,1,0,0,0,0,1,0,0,0,0,1,0,0,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,
    1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
    0,1,1,1,0,0,1,1,1,1,1,0,1,1,1,1,1,0,0,1,1,1,0,0,0,0,0,0,1,0,0,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,0,
    0,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,1,1,1,0,0,0,1,1,1,0,0,1,1,1,1,0,0,0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,1,0,1,1,1,1,1,0,0,1,1,1,0,0,
    1,0,0,0,1,0,1,1,1,1,1,0,0,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,0,1,1,1,0,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,0,1,0,0,1,0,0,0,0,0,1,1,0,1,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    1,0,0,1,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,
    1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,1,0,1,0,1,0,1,1,0,0,1,0,1,0,0,0,1,0,
    1,0,0,1,1,0,1,1,1,1,1,0,1,1,1,1,0,0,1,0,0,0,0,0,1,0,0,0,1,0,1,1,1,1,0,0,1,1,1,1,0,0,1,0,0,0,0,0,
    1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,1,0,1,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,1,0,1,0,1,0,0,0,1,0,
    1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,0,1,0,0,0,1,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,1,0,0,1,0,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    0,1,1,1,0,0,1,0,0,0,1,0,1,1,1,1,0,0,0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,1,1,0,1,0,0,0,0,0,0,1,1,1,1,0,
    1,0,0,0,1,0,1,1,1,1,1,0,0,1,1,1,0,0,1,0,0,0,1,0,1,1,1,1,1,0,1,0,0,0,1,0,1,0,0,0,1,0,0,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,0,0,0,1,1,1,0,0,1,1,1,1,0,0,0,1,1,1,0,0,1,1,1,1,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,1,1,1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    1,0,0,0,1,0,0,1,0,1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,1,0,0,1,0,0,0,1,0,
    0,1,0,1,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,
    1,1,1,1,0,0,1,0,0,0,1,0,1,1,1,1,0,0,0,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,1,0,0,1,0,1,0,1,0,
    0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,1,0,1,0,1,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,1,0,0,1,0,1,0,1,0,
    0,1,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,1,0,0,1,1,0,1,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,1,0,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,1,1,1,0,0,1,0,0,0,1,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,0,1,0,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,1,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,1,1,1,1,1,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,1,1,1,0,0,1,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,1,0,0,1,1,1,0,0,0,0,1,0,0,0,0,1,1,1,1,0,
    1,1,1,1,0,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,1,1,0,1,0,0,1,1,1,1,0,0,0,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,0,0,1,0,0,0,0,0,0,1,1,1,1,0,1,0,0,0,1,0,1,1,1,1,1,0,1,0,0,0,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    0,0,0,0,0,0,0,1,1,1,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,1,0,1,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,1,1,1,0,0,0,0,0,1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,1,0,0,0,0,1,1,1,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,0,1,0,0,1,0,0,0,0,1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    0,0,0,0,0,0,0,1,1,1,1,0,1,1,1,1,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0,0,0,0,1,0,0,0,0,0,0,0,1,0,
    1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,1,0,1,0,1,0,0,0,1,0,0,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,1,0,1,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,1,0,1,0,0,0,0,0,0,0,0,
    1,1,1,1,0,0,0,1,1,1,1,0,1,0,1,1,0,0,0,1,1,1,0,0,1,1,1,1,1,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,1,0,0,1,0,1,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,1,0,
    0,1,0,1,0,0,1,0,0,0,1,0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,1,0,1,0,0,0,1,0,1,0,0,0,0,0,0,1,1,1,0,0,0,0,1,0,0,0,1,0,0,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,
    0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,0,0,0,1,1,1,1,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,1,0,1,0,0,1,0,1,0,1,0,
    0,1,0,1,0,0,0,1,1,1,1,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,1,1,0,0,1,1,1,1,0,0,0,1,0,0,0,0,1,0,1,0,0,
    1,0,0,0,1,0,0,0,0,0,1,0,1,1,1,1,1,0,0,0,1,1,0,0,0,0,1,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
