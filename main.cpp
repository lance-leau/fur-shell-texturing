// clang-format off
#include <GL/glew.h> // glew doit etre tout en haut
// clang-format on
#include <GL/freeglut.h>
#include <fstream>
#include <iostream>
#include <vector>

#include "bunny.hh"

#define TEST_OPENGL_ERROR()                                                    \
    do                                                                         \
    {                                                                          \
        GLenum err = glGetError();                                             \
        if (err != GL_NO_ERROR)                                                \
            std::cerr << "OpenGL ERROR! " << __LINE__ << std::endl;            \
    } while (0)

GLuint bunny_vao_id;
GLuint program_id;

// fur parameters
int shell_count = 32;
float fur_length = 0.04f;
float strand_density = 150.0f;

float anim_time = 0.0f;

void update_uniforms()
{
    glUniform1f(glGetUniformLocation(program_id, "anim_time"), anim_time);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(program_id, "strand_density"),
                strand_density);
    TEST_OPENGL_ERROR();
    glUniform1i(glGetUniformLocation(program_id, "shell_count"), shell_count);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(program_id, "fur_length"), fur_length);
    TEST_OPENGL_ERROR();
}

// the scene is drawn in a multisample FBO (the window has no multisampling),
// then copied to the window
int msaa_samples = 8;
int window_width = 1024;
int window_height = 1024;
GLuint msaa_fbo_id;
GLuint msaa_color_rb;
GLuint msaa_depth_rb;

void resize_msaa_buffers()
{
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_color_rb);
    TEST_OPENGL_ERROR();
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_samples, GL_RGBA8,
                                     window_width, window_height);
    TEST_OPENGL_ERROR();
    glBindRenderbuffer(GL_RENDERBUFFER, msaa_depth_rb);
    TEST_OPENGL_ERROR();
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_samples,
                                     GL_DEPTH_COMPONENT24, window_width,
                                     window_height);
    TEST_OPENGL_ERROR();
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

bool init_msaa_fbo()
{
    GLint max_samples;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    TEST_OPENGL_ERROR();
    if (msaa_samples > max_samples)
        msaa_samples = max_samples;
    std::cout << "MSAA samples: " << msaa_samples << std::endl;

    glGenRenderbuffers(1, &msaa_color_rb);
    TEST_OPENGL_ERROR();
    glGenRenderbuffers(1, &msaa_depth_rb);
    TEST_OPENGL_ERROR();
    resize_msaa_buffers();

    glGenFramebuffers(1, &msaa_fbo_id);
    TEST_OPENGL_ERROR();
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);
    TEST_OPENGL_ERROR();
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, msaa_color_rb);
    TEST_OPENGL_ERROR();
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, msaa_depth_rb);
    TEST_OPENGL_ERROR();

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "FBO incomplete: 0x" << std::hex << status << std::dec
                  << std::endl;
        return false;
    }
    return true;
}

void window_resize(int width, int height)
{
    window_width = width;
    window_height = height;
    resize_msaa_buffers();
    glViewport(0, 0, width, height);
    TEST_OPENGL_ERROR();
}

void display()
{
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);
    TEST_OPENGL_ERROR();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    TEST_OPENGL_ERROR();
    update_uniforms();
    glBindVertexArray(bunny_vao_id);
    TEST_OPENGL_ERROR();
    // one instance per shell, the vertex shader uses gl_InstanceID
    glDrawArraysInstanced(GL_TRIANGLES, 0, vertex_buffer_data.size() / 3,
                          shell_count);
    TEST_OPENGL_ERROR();
    glBindVertexArray(0);
    TEST_OPENGL_ERROR();

    // the blit averages the samples of each pixel
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_id);
    TEST_OPENGL_ERROR();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    TEST_OPENGL_ERROR();
    glBlitFramebuffer(0, 0, window_width, window_height, 0, 0, window_width,
                      window_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    TEST_OPENGL_ERROR();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    TEST_OPENGL_ERROR();
    glutSwapBuffers();
}

void timer(int)
{
    anim_time += 0.033f;
    glutPostRedisplay();
    glutTimerFunc(33, timer, 0);
}

void keyboard(unsigned char key, int, int)
{
    if (key == '+')
        shell_count++;
    else if (key == '-' && shell_count > 2)
        shell_count--;
    else if (key == 'L')
        fur_length *= 1.1f;
    else if (key == 'l')
        fur_length /= 1.1f;
    else if (key == 'D')
        strand_density *= 1.1f;
    else if (key == 'd')
        strand_density /= 1.1f;
    else
        return;
    std::cout << "shells: " << shell_count << ", length: " << fur_length
              << ", density: " << strand_density << std::endl;
}

void init_glut(int& argc, char* argv[])
{
    // glewExperimental = GL_TRUE;
    glutInit(&argc, argv);
    glutInitContextVersion(4, 5);
    glutInitContextProfile(GLUT_CORE_PROFILE | GLUT_DEBUG);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(1024, 1024);
    glutInitWindowPosition(100, 100);
    glutCreateWindow("Fur shell texturing");
    glutDisplayFunc(display);
    glutReshapeFunc(window_resize);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(33, timer, 0);
}

bool init_glew()
{
    if (glewInit())
    {
        std::cerr << " Error while initializing glew";
        return false;
    }
    return true;
}

void init_GL()
{
    glPointSize(0.1f);
    TEST_OPENGL_ERROR();
    glEnable(GL_DEPTH_TEST);
    TEST_OPENGL_ERROR();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    TEST_OPENGL_ERROR();
    glEnable(GL_CULL_FACE);
    TEST_OPENGL_ERROR();
    // the fragment shader runs once per sample (not once per pixel), so the
    // discard of the strands is done per sample: smooth strand edges
    glEnable(GL_SAMPLE_SHADING);
    TEST_OPENGL_ERROR();
    glMinSampleShading(1.0f);
    TEST_OPENGL_ERROR();
    glClearColor(0.4, 0.4, 0.4, 1.0);
    TEST_OPENGL_ERROR();
}

void init_object_vbo()
{
    int max_nb_vbo = 5;
    int nb_vbo = 0;
    int index_vbo = 0;
    GLuint vbo_ids[max_nb_vbo];

    GLint vertex_location = glGetAttribLocation(program_id, "position");
    TEST_OPENGL_ERROR();
    GLint normal_smooth_location =
        glGetAttribLocation(program_id, "normalSmooth");
    TEST_OPENGL_ERROR();

    glGenVertexArrays(1, &bunny_vao_id);
    TEST_OPENGL_ERROR();
    glBindVertexArray(bunny_vao_id);
    TEST_OPENGL_ERROR();

    if (vertex_location != -1)
        nb_vbo++;
    if (normal_smooth_location != -1)
        nb_vbo++;
    glGenBuffers(nb_vbo, vbo_ids);
    TEST_OPENGL_ERROR();

    if (vertex_location != -1)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_ids[index_vbo++]);
        TEST_OPENGL_ERROR();
        glBufferData(GL_ARRAY_BUFFER, vertex_buffer_data.size() * sizeof(float),
                     vertex_buffer_data.data(), GL_STATIC_DRAW);
        TEST_OPENGL_ERROR();
        glVertexAttribPointer(vertex_location, 3, GL_FLOAT, GL_FALSE, 0, 0);
        TEST_OPENGL_ERROR();
        glEnableVertexAttribArray(vertex_location);
        TEST_OPENGL_ERROR();
    }

    if (normal_smooth_location != -1)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_ids[index_vbo++]);
        TEST_OPENGL_ERROR();
        glBufferData(GL_ARRAY_BUFFER,
                     normal_smooth_buffer_data.size() * sizeof(float),
                     normal_smooth_buffer_data.data(), GL_STATIC_DRAW);
        TEST_OPENGL_ERROR();
        glVertexAttribPointer(normal_smooth_location, 3, GL_FLOAT, GL_FALSE, 0,
                              0);
        TEST_OPENGL_ERROR();
        glEnableVertexAttribArray(normal_smooth_location);
        TEST_OPENGL_ERROR();
    }

    glBindVertexArray(0);
}

std::string load(const std::string& filename)
{
    std::ifstream input_src_file(filename, std::ios::in);
    std::string ligne;
    std::string file_content = "";
    if (input_src_file.fail())
    {
        std::cerr << "FAIL\n";
        return "";
    }
    while (getline(input_src_file, ligne))
    {
        file_content = file_content + ligne + "\n";
    }
    file_content += '\0';
    input_src_file.close();
    return file_content;
}

bool init_shaders()
{
    std::string vertex_src = load("vertex.shd");
    std::string fragment_src = load("fragment.shd");
    GLuint shader_id[2];
    GLint compile_status = GL_TRUE;
    char* vertex_shd_src =
        (char*)std::malloc(vertex_src.length() * sizeof(char));
    char* fragment_shd_src =
        (char*)std::malloc(fragment_src.length() * sizeof(char));
    vertex_src.copy(vertex_shd_src, vertex_src.length());
    fragment_src.copy(fragment_shd_src, fragment_src.length());

    shader_id[0] = glCreateShader(GL_VERTEX_SHADER);
    TEST_OPENGL_ERROR();
    shader_id[1] = glCreateShader(GL_FRAGMENT_SHADER);
    TEST_OPENGL_ERROR();

    glShaderSource(shader_id[0], 1, (const GLchar**)&(vertex_shd_src), 0);
    TEST_OPENGL_ERROR();
    glShaderSource(shader_id[1], 1, (const GLchar**)&(fragment_shd_src), 0);
    TEST_OPENGL_ERROR();
    for (int i = 0; i < 2; i++)
    {
        glCompileShader(shader_id[i]);
        TEST_OPENGL_ERROR();
        glGetShaderiv(shader_id[i], GL_COMPILE_STATUS, &compile_status);
        if (compile_status != GL_TRUE)
        {
            GLint log_size;
            char* shader_log;
            glGetShaderiv(shader_id[i], GL_INFO_LOG_LENGTH, &log_size);
            shader_log = (char*)std::malloc(
                log_size + 1); /* +1 pour le caractere de fin de chaine '\0' */
            if (shader_log != 0)
            {
                glGetShaderInfoLog(shader_id[i], log_size, &log_size,
                                   shader_log);
                std::cerr << "SHADER " << i << ": " << shader_log << std::endl;
                std::free(shader_log);
            }
            std::free(vertex_shd_src);
            std::free(fragment_shd_src);
            glDeleteShader(shader_id[0]);
            glDeleteShader(shader_id[1]);
            return false;
        }
    }
    std::free(vertex_shd_src);
    std::free(fragment_shd_src);

    GLint link_status = GL_TRUE;
    program_id = glCreateProgram();
    TEST_OPENGL_ERROR();
    if (program_id == 0)
        return false;
    for (int i = 0; i < 2; i++)
    {
        glAttachShader(program_id, shader_id[i]);
        TEST_OPENGL_ERROR();
    }
    glLinkProgram(program_id);
    TEST_OPENGL_ERROR();
    glGetProgramiv(program_id, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE)
    {
        GLint log_size;
        char* program_log;
        glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &log_size);
        program_log = (char*)std::malloc(
            log_size + 1); /* +1 pour le caractere de fin de chaine '\0' */
        if (program_log != 0)
        {
            glGetProgramInfoLog(program_id, log_size, &log_size, program_log);
            std::cerr << "Program " << program_log << std::endl;
            std::free(program_log);
        }
        glDeleteProgram(program_id);
        TEST_OPENGL_ERROR();
        glDeleteShader(shader_id[0]);
        TEST_OPENGL_ERROR();
        glDeleteShader(shader_id[1]);
        TEST_OPENGL_ERROR();
        program_id = 0;
        return false;
    }
    glUseProgram(program_id);
    TEST_OPENGL_ERROR();
    return true;
}

int main(int argc, char* argv[])
{
    init_glut(argc, argv);
    if (!init_glew())
        std::exit(-1);
    init_GL();
    if (!init_msaa_fbo())
        std::exit(-1);
    init_shaders();
    init_object_vbo();
    glutMainLoop();
}
