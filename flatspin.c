#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdio.h>

#define GLSL(__source) "#version 150 core\n" #__source

#define _MAX_VERTICES   4096
static float _vertices[_MAX_VERTICES][5];
static int _vertices_count;

static void flatspin_update();

static inline void add_vertex(float x, float y, float r, float g, float b)
{
    if (_vertices_count >= _MAX_VERTICES) {
        fprintf(stderr, "> < Too many vertices!");
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
    float r, float g, float b)
{
    add_vertex(x, y, r, g, b);
    add_vertex(x + w, y, r, g, b);
    add_vertex(x + w, y + h, r, g, b);
    add_vertex(x, y, r, g, b);
    add_vertex(x + w, y + h, r, g, b);
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
    fprintf(stderr, "OvO Compilation log for %s shader\n",
        (type == GL_VERTEX_SHADER ? "vertex" :
         type == GL_FRAGMENT_SHADER ? "fragment" : "unknown (!)"));
    fputs(msg_buf, stderr);
    fprintf(stderr, "=v= End\n");
    if (status != GL_TRUE) {
        fprintf(stderr, "> < Shader compilation failed\n");
        return 0;
    }

    return shader_id;
}

int main()
{
    // -- Initialization --

    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    GLFWwindow *window = glfwCreateWindow(960, 540, "bmflatspin", NULL, NULL);
    if (window == NULL) {
        fprintf(stderr, "> < Cannot initialize GLFW\n");
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "> < Cannot initialize GLEW\n");
        return -1;
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
        glClearColor(0.15f, 0.1f, 0.3f, 1.0f);
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

static void flatspin_update()
{
    _vertices_count = 0;
    float ovo = sin(glfwGetTime()) * -0.2f + 0.6f;
    add_vertex(0.0f, 0.5f, 1.0f, 1.0f, 0.3f);
    add_vertex(0.5f, -0.5f, 1.0f, 0.9f, 0.4f);
    add_vertex(-0.5f, -0.5f, 1.0f, ovo, 0.3f);
    add_rect(-0.1f, -0.7f, 0.2f, 0.2f, 1.0f, 0.5f, 0.3f);
}
