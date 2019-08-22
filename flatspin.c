#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "bmflat.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define GLSL(__source) "#version 150 core\n" #__source

#define _MAX_VERTICES   4096
static float _vertices[_MAX_VERTICES][5];
static int _vertices_count;

static int flatspin_init();
static void flatspin_update();

static const char *flatspin_bmspath;
static const char *flatspin_basepath;

static inline void add_vertex(float x, float y, float r, float g, float b)
{
    if (_vertices_count >= _MAX_VERTICES) {
        fprintf(stderr, "> <  Too many vertices!");
        return;
    }
    _vertices[_vertices_count][0] = x;
    _vertices[_vertices_count][1] = y;
    _vertices[_vertices_count][2] = r;
    _vertices[_vertices_count][3] = g;
    _vertices[_vertices_count++][4] = b;
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
        flatspin_basepath = (char *)malloc(p + 1);
        memcpy(flatspin_basepath, flatspin_bmspath, p + 1);
    }
    fprintf(stderr, "^ ^  Asset search path: %s\n", flatspin_basepath);

    int result = flatspin_init();
    if (result != 0) return result;

    // -- Initialization --

    if (!glfwInit()) {
        fprintf(stderr, "> <  Cannot initialize GLFW\n");
        return 2;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    GLFWwindow *window = glfwCreateWindow(960, 540, "bmflatspin", NULL, NULL);
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

    // -- Resource allocation --
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    const char *vshader_source = GLSL(
        in vec2 ppp;
        in vec3 qwq;
        out vec3 qwq_frag;
        void main()
        {
            gl_Position = vec4(ppp, 0.0, 1.0);
            qwq_frag = qwq;
        }
    );
    GLuint vshader = load_shader(GL_VERTEX_SHADER, vshader_source);

    const char *fshader_source = GLSL(
        in vec3 qwq_frag;
        out vec4 ooo;
        void main()
        {
            ooo = vec4(qwq_frag, 1.0f);
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
        5 * sizeof(float), (void *)0);

    GLuint qwq_attrib_index = glGetAttribLocation(prog, "qwq");
    glEnableVertexAttribArray(qwq_attrib_index);
    glVertexAttribPointer(qwq_attrib_index, 3, GL_FLOAT, GL_FALSE,
        5 * sizeof(float), (void *)(2 * sizeof(float)));

    // -- Event/render loop --

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.7f, 0.7f, 0.7f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        flatspin_update();

        glBufferData(GL_ARRAY_BUFFER,
            _vertices_count * 5 * sizeof(float), _vertices, GL_STREAM_DRAW);
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

#define SCRATCH_WIDTH   4
#define KEY_WIDTH       3
#define BGTRACK_WIDTH   2

static int flatspin_init()
{
    char *src = read_file(flatspin_bmspath);
    if (src == NULL) {
        fprintf(stderr, "> <  Cannot load BMS file %s\n", flatspin_bmspath);
        return 1;
    }

    msgs_count = bm_load(&chart, src);
    bm_to_seq(&chart, &seq);
}

static inline void draw_track(
    float x, float w, float r, float g, float b)
{
    add_rect(x, -1, w, 2, r * 0.3, g * 0.3, b * 0.3, false);
    for (int i = 0; i <= 10; i++)
        add_rect(x, -0.7 + i * 0.1, w, i == 10 ? 0.4 : 0.03, r, g, b, true);
}

static void flatspin_update()
{
    _vertices_count = 0;

    float unit = 2.0f / (SCRATCH_WIDTH + KEY_WIDTH * 7 +
        BGTRACK_WIDTH * chart.tracks.background_count);

    draw_track(-1.0f, unit * SCRATCH_WIDTH, 1.0f, 0.4f, 0.3f);
    for (int i = 0; i < 7; i++) {
        draw_track(
            -1.0f + unit * (SCRATCH_WIDTH + KEY_WIDTH * i),
            unit * KEY_WIDTH,
            i % 2 == 0 ? 1.0f : 0.5f,
            i % 2 == 0 ? 1.0f : 0.5f,
            i % 2 == 0 ? 1.0f : 1.0f
        );
    }
    for (int i = 0; i < chart.tracks.background_count; i++) {
        draw_track(
            -1.0f + unit * (SCRATCH_WIDTH + KEY_WIDTH * 7 + BGTRACK_WIDTH * i),
            unit * BGTRACK_WIDTH,
            i % 2 == 0 ? 1.0f : 0.6f,
            i % 2 == 0 ? 0.9f : 0.8f,
            i % 2 == 0 ? 0.6f : 0.5f
        );
    }

    for (int i = 0; i < seq.event_count; i++) {
    }
}
