#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "bmflat.h"

#define DR_WAV_IMPLEMENTATION
#include "miniaudio/extras/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "miniaudio/extras/dr_mp3.h"
#include "miniaudio/extras/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define GLSL(__source) "#version 150 core\n" #__source

#define TEX_W   96
#define TEX_H   48

// ffmpeg -f rawvideo -pix_fmt gray - -i
static unsigned char tex_data[TEX_H * TEX_W];

#define _MAX_VERTICES   4096
struct vertex {
    float x, y;
    float r, g, b, a;
    float tx, ty;
};
static struct vertex _vertices[_MAX_VERTICES];
static int _vertices_count;

static int flatspin_init();
static void flatspin_update(float dt);

static char *flatspin_bmspath;
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

static inline void add_vertex(float x, float y, float r, float g, float b)
{
    add_vertex_tex(x, y, r, g, b, 1, -1, -1);
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

#define TEXT_W  (1.0f / 30)
#define TEXT_H  (TEXT_W * 2)

static inline void add_char(
    float x, float y, float r, float g, float b, float a, char ch)
{
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
    ma_device *device, float *output, const float *input, ma_uint32 nframes);
static ma_device audio_device;

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "=~=  Usage: %s <path to BMS>\n", argv[0]);
        return 0;
    }

    // Extract path and executable path
    flatspin_bmspath = argv[1];
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

    int result = flatspin_init();
    if (result != 0) return result;

    // Initialize miniaudio
    ma_device_config dev_config =
        ma_device_config_init(ma_device_type_playback);
    dev_config.playback.format = ma_format_f32;
    dev_config.playback.channels = 2;
    dev_config.sampleRate = 44100;
    dev_config.dataCallback = (ma_device_callback_proc)audio_data_callback;

    if (ma_device_init(NULL, &dev_config, &audio_device) != MA_SUCCESS ||
        ma_device_start(&audio_device) != MA_SUCCESS)
    {
        fprintf(stderr, "> <  Cannot start audio playback");
        return 3;
    }

    // -- Initialization --

    if (!glfwInit()) {
        fprintf(stderr, "> <  Cannot initialize GLFW\n");
        return 2;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_TRUE);

    window = glfwCreateWindow(960, 540, "bmflatspin", NULL, NULL);
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
    GLuint vshader = load_shader(GL_VERTEX_SHADER, vshader_source);

    const char *fshader_source = GLSL(
        in vec4 qwq_frag;
        in vec2 uwu_frag;
        uniform sampler2D tex;
        out vec4 ooo;
        void main()
        {
            if (uwu_frag.x < -0.5f) {
                ooo = vec4(
                    qwq_frag.r, qwq_frag.g, qwq_frag.b,
                    1.0f);
            } else {
                ooo = vec4(
                    qwq_frag.r, qwq_frag.g, qwq_frag.b,
                    qwq_frag.a * texture(tex, uwu_frag));
            }
        }
    );
    GLuint fshader = load_shader(GL_FRAGMENT_SHADER, fshader_source);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vshader);
    glAttachShader(prog, fshader);
    glBindFragDataLocation(prog, 0, "ooo");
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
    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, TEX_W, TEX_H,
        0, GL_RED, GL_UNSIGNED_BYTE, tex_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);

    // -- Event/render loop --

    float last_time = 0, cur_time;

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.7f, 0.7f, 0.7f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        cur_time = glfwGetTime();
        flatspin_update(cur_time - last_time);
        last_time = cur_time;

        glBufferData(GL_ARRAY_BUFFER,
            _vertices_count * sizeof(struct vertex), _vertices, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, _vertices_count);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

// -- Application logic --

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return NULL;

    char *buf = NULL;

    do {
        if (fseek(f, 0, SEEK_END) != 0) break;
        long len = ftell(f);
        if (fseek(f, 0, SEEK_SET) != 0) break;
        if ((buf = (char *)malloc(len)) == NULL) break;
        if (fread(buf, len, 1, f) != 1) { free(buf); buf = NULL; break; }
    } while (0);

    fclose(f);
    return buf;
}

static int msgs_count;
static struct bm_chart chart;
static struct bm_seq seq;

#define MSGS_FADE_OUT_TIME  0.2
static float msgs_show_time = -MSGS_FADE_OUT_TIME;

static float *pcm[BM_INDEX_MAX] = { NULL };
static ma_uint64 pcm_len[BM_INDEX_MAX] = { 0 };

#define TOTAL_TRACKS    (8 + BM_BGM_TRACKS)
static int track_wave[TOTAL_TRACKS];
static ma_uint64 track_wave_pos[TOTAL_TRACKS];

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

static float unit;

static float play_pos;
static float scroll_speed;
static float fwd_range;
static float bwd_range;
#define Y_POS(__pos)    (((__pos) - play_pos) * scroll_speed + HITLINE_POS)

static bool playing = false;
static float current_bpm;   // Will be re-initialized on playback start
static int event_ptr;

static float delta_ss_rate; // For easing of scroll speed changes
static float delta_ss_time;
#define SS_MIN  (0.1f / 48)
#define SS_MAX  (1.0f / 48)
#define SS_DELTA    (0.05f / 48)
#define SS_INITIAL  (0.4f / 48)

static void audio_data_callback(
    ma_device *device, float *output, const float *input, ma_uint32 nframes)
{
    ma_mutex_lock(&device->lock);

    ma_zero_pcm_frames(output, nframes, ma_format_f32, 2);
    for (int i = 0; i < TOTAL_TRACKS; i++) {
        int wave = track_wave[i];
        if (wave != -1) {
            int start = track_wave_pos[i];
            int j;
            for (j = 0; j < nframes && start + j < pcm_len[wave]; j++) {
                float lsmp = pcm[wave][(start + j) * 2];
                float rsmp = pcm[wave][(start + j) * 2 + 1];
                output[j * 2] += lsmp;
                output[j * 2 + 1] += rsmp;
                msq_accum[i] += lsmp * lsmp + rsmp * rsmp;
            }
            track_wave_pos[i] += j;
        }
    }

    msq_accum_size += nframes;

    ma_mutex_unlock(&device->lock);

    (void)input;    // Unused
}

static inline void delta_ss_step(float dt)
{
    if (delta_ss_time <= 0) return;
    if (dt > delta_ss_time) dt = delta_ss_time;
    scroll_speed += delta_ss_rate * dt;
    delta_ss_time -= dt;
    if (scroll_speed < SS_MIN) scroll_speed = SS_MIN;
    if (scroll_speed > SS_MAX) scroll_speed = SS_MAX;
    fwd_range = (1.1 - HITLINE_POS) / scroll_speed;
    bwd_range = (HITLINE_POS + 1.1) / scroll_speed;
}

static inline void delta_ss_submit(float delta, float time)
{
    float total_delta = delta + delta_ss_rate * delta_ss_time;
    delta_ss_rate = total_delta / time;
    delta_ss_time = time;
}

static const char *base36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static inline ma_result try_load_audio(
    const char *path, ma_decoder_config *cfg, ma_uint64 *len, float **ptr)
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

static int flatspin_init()
{
    char *src = read_file(flatspin_bmspath);
    if (src == NULL) {
        fprintf(stderr, "> <  Cannot load BMS file %s\n", flatspin_bmspath);
        return 1;
    }

    msgs_count = bm_load(&chart, src);
    bm_to_seq(&chart, &seq);
    msgs_show_time = 10;

    unit = 2.0f / (SCRATCH_WIDTH + KEY_WIDTH * 7 +
        BGTRACK_WIDTH * chart.tracks.background_count);

    play_pos = 0;
    scroll_speed = SS_INITIAL;  // Screen Y units per 1/48 beat
    fwd_range = (1.1 - HITLINE_POS) / scroll_speed;
    bwd_range = (HITLINE_POS + 1.1) / scroll_speed;

    delta_ss_rate = delta_ss_time = 0;

    // Load PCM data
    ma_decoder_config dec_config = ma_decoder_config_init(ma_format_f32, 2, 44100);
    char s[1024] = { 0 };
    strcpy(s, flatspin_basepath);
    int len = strlen(flatspin_basepath);
    for (int i = 0; i < BM_INDEX_MAX; i++) if (chart.tables.wav[i] != NULL) {
        strncpy(s + len, chart.tables.wav[i], sizeof(s) - len - 1);
        ma_result result = try_load_audio(s, &dec_config, &pcm_len[i], &pcm[i]);
        if (result != MA_SUCCESS) {
            pcm_len[i] = 0;
            pcm[i] = NULL;
            fprintf(stderr, "> <  Cannot load wave #%c%c %s (error code %d)\n",
                base36[i / 36], base36[i % 36], s, result);
        } else {
            fprintf(stderr, "= =  Loaded wave #%c%c %s; length %.3f seconds\n",
                base36[i / 36], base36[i % 36],
                chart.tables.wav[i], (double)pcm_len[i] / 44100);
        }
    }

    for (int i = 0; i < TOTAL_TRACKS; i++) {
        track_wave[i] = -1;
        track_wave_pos[i] = 0;
    }

    return 0;
}

static inline int track_index(int id)
{
    if (id == 16)
        return 0;
    else if (id >= 11 && id <= 19 && id != 17)
        return (id < 17 ? id - 10 : id - 12);
    else if (id <= 0)
        return 8 - id;
    else return -1;
}

static inline void track_attr(
    int id, float *x, float *w, float *r, float *g, float *b)
{
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

    static int keys_prev[6] = { GLFW_RELEASE }; // GLFW_RELEASE == 0
    int keys[6] = {
        glfwGetKey(window, GLFW_KEY_UP),
        glfwGetKey(window, GLFW_KEY_DOWN),
        glfwGetKey(window, GLFW_KEY_LEFT),
        glfwGetKey(window, GLFW_KEY_RIGHT),
        glfwGetKey(window, GLFW_KEY_SPACE),
        glfwGetKey(window, GLFW_KEY_ENTER)
    };

    if (keys[2] == GLFW_PRESS && keys_prev[2] == GLFW_RELEASE) {
        // Left: scroll-
        delta_ss_submit(-SS_DELTA, 0.1);
    } else if (keys[3] == GLFW_PRESS && keys_prev[3] == GLFW_RELEASE) {
        // Right: scroll+
        delta_ss_submit(+SS_DELTA, 0.1);
    }

    int mul =
        (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
         glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) ? 4 : 1;
    if (keys[0] == GLFW_PRESS && keys[1] == GLFW_RELEASE) {
        // Up: play pos+
        play_pos += dt * 288 / (scroll_speed / SS_INITIAL) * mul;
        playing = false;
        play_cut = true;
    } else if (keys[1] == GLFW_PRESS && keys[0] == GLFW_RELEASE) {
        // Down: play pos-
        play_pos -= dt * 288 / (scroll_speed / SS_INITIAL) * mul;
        playing = false;
        play_cut = true;
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
    } else if (play_cut) {
        // Stop all sounds
        ma_mutex_lock(&audio_device.lock);
        for (int i = 0; i < TOTAL_TRACKS; i++) track_wave[i] = -1;
        ma_mutex_unlock(&audio_device.lock);
    }

    // Fade out log messages on any movement
    if (play_pos != 0 && msgs_show_time > 0) msgs_show_time = 0;

    memcpy(keys_prev, keys, sizeof keys);

    // -- Updates --

    if (msgs_show_time > -MSGS_FADE_OUT_TIME)
        msgs_show_time -= dt;

    delta_ss_step(dt);

    ma_mutex_lock(&audio_device.lock);

    if (playing) {
        play_pos += dt * current_bpm * (48.0f / 60.0f);

        while (event_ptr < seq.event_count && seq.events[event_ptr].pos <= play_pos) {
            struct bm_event ev = seq.events[event_ptr];
            switch (ev.type) {
            case BM_TEMPO_CHANGE:
                current_bpm = ev.value_f;
                break;
            case BM_NOTE:
            case BM_NOTE_LONG:
                track_wave[track_index(ev.track)] = ev.value;
                track_wave_pos[track_index(ev.track)] = 0;
                break;
            // Not really
            //case BM_NOTE_OFF:
            //    track_wave[track_index(ev.track)] = -1;
            //    break;
            default: break;
            }
            event_ptr++;
        }
    }

    if (play_pos < 0) {
        play_pos = 0;
    } else if (play_pos > seq.events[seq.event_count - 1].pos) {
        play_pos = seq.events[seq.event_count - 1].pos;
        playing = false;
    }

    // Audio RMS data

    if (msq_accum_size != 0) {
        #define process_track(__i) do { \
            int index = track_index(__i); \
            float z = msq_accum[index] / msq_accum_size; \
            msq_sum[index] -= msq_gframe[index][msq_ptr[index]]; \
            msq_gframe[index][msq_ptr[index]] = z; \
            msq_sum[index] += z; \
            /* Floating point errors may occur */ \
            if (msq_sum[index] < 1e-4) msq_sum[index] = 0; \
            msq_ptr[index] = (msq_ptr[index] + 1) % RMS_WINDOW_SIZE; \
            msq_accum[index] = 0; \
        } while (0)

        for (int i = 11; i <= 19; i++)
            if (i != 17) process_track(i);
        for (int i = 0; i < chart.tracks.background_count; i++)
            process_track(-i);

        msq_accum_size = 0;
    }

    ma_mutex_unlock(&audio_device.lock);

    // -- Drawing --

    _vertices_count = 0;

    for (int i = 11; i <= 19; i++)
        if (i != 17) draw_track_background(i);
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
        if (seq.events[i].type == BM_BARLINE) {
            if (i == seq.event_count - 1) {
                add_rect(-1, Y_POS(seq.events[i].pos), 2, 0.01, 0.6, 0.7, 0.4, false);
                add_text(1 - TEXT_W * 11.5, Y_POS(seq.events[i].pos) + TEXT_H / 8,
                    0.6, 0.7, 0.4, 1.0, "Fin \\(^ ^)/");
            } else {
                add_rect(-1, Y_POS(seq.events[i].pos), 2, 0.01, 0.4, 0.4, 0.4, false);
                sprintf(s, "#%03d", seq.events[i].value);
                add_text(1 - TEXT_W * 4.5, Y_POS(seq.events[i].pos) + TEXT_H / 8,
                    0.4, 0.4, 0.4, 1.0, s);
            }
        } else if (seq.events[i].type == BM_TEMPO_CHANGE) {
            add_rect(-1, Y_POS(seq.events[i].pos), 2, 0.01, 0.5, 0.5, 0.4, false);
            sprintf(s, "BPM %06.2f", seq.events[i].value_f);
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

    add_rect(-1, HITLINE_POS, 2, 0.01, 1.0, 0.7, 0.4, false);

    // Messages from the parser
    if (msgs_show_time > -MSGS_FADE_OUT_TIME) {
        char s[10];
        float y = 0.95 - TEXT_H * 1.75;
        float line_w = 1.9 - TEXT_W * 5;
        float alpha = (msgs_show_time > 0 ? 1 : 1 + msgs_show_time / MSGS_FADE_OUT_TIME);
        for (int i = 0; i < msgs_count; i++) {
            if (bm_logs[i].line != -1) {
                snprintf(s, sizeof s, "L%3d", bm_logs[i].line);
                add_text(-0.95, y, 1.0, 1.0, 0.7, alpha, s);
            } else {
                add_char(-0.95 + TEXT_W * 3, y, 1.0, 1.0, 0.7, alpha, '>');
            }
            int lines = add_text_w(-0.95 + TEXT_W * 5, y,
                line_w, 0.95, 0.95, 0.95, alpha, bm_logs[i].message);
            y -= TEXT_H * (lines + 0.75);
        }
    }
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
