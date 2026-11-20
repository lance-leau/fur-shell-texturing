// clang-format off
#include <GL/glew.h> // glew doit etre tout en haut
// clang-format on
#include <GL/freeglut.h>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
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

// the bunny is a triangle soup: a vertex is repeated in every triangle that
// uses it. The identical vertices are merged and drawn with an index buffer,
// so the vertex shader runs about 6 times less.
std::vector<GLfloat> bunny_positions;
std::vector<GLfloat> bunny_normals;
std::vector<GLuint> bunny_indices;

void index_bunny()
{
    std::map<std::array<GLfloat, 6>, GLuint> vertex_index;
    for (size_t i = 0; i < vertex_buffer_data.size(); i += 3)
    {
        std::array<GLfloat, 6> vertex = { vertex_buffer_data[i],
                                          vertex_buffer_data[i + 1],
                                          vertex_buffer_data[i + 2],
                                          normal_smooth_buffer_data[i],
                                          normal_smooth_buffer_data[i + 1],
                                          normal_smooth_buffer_data[i + 2] };
        GLuint new_index = bunny_positions.size() / 3;
        // insert does nothing if the vertex is already known
        auto result = vertex_index.insert({ vertex, new_index });
        bunny_indices.push_back(result.first->second);
        if (result.second)
        {
            bunny_positions.insert(bunny_positions.end(), vertex.begin(),
                                   vertex.begin() + 3);
            bunny_normals.insert(bunny_normals.end(), vertex.begin() + 3,
                                 vertex.end());
        }
    }
    std::cout << "bunny: " << bunny_indices.size() << " vertices, "
              << bunny_positions.size() / 3 << " unique" << std::endl;
}

// fur parameters
int shell_count = 64;
float fur_length = 0.05f;
float strand_density = 250.0f;

float anim_time = 0.0f;

// FPS counter and GPU time of the fur draw call, printed every second
int frame_count = 0;
int fps_start_time = 0;
GLuint gpu_time_query;
double gpu_time_ms = 0.0;

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

void window_resize(int width, int height)
{
    glViewport(0, 0, width, height);
    TEST_OPENGL_ERROR();
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    TEST_OPENGL_ERROR();
    update_uniforms();
    glBindVertexArray(bunny_vao_id);
    TEST_OPENGL_ERROR();
    // the GPU measures how long the draw call takes on its side
    glBeginQuery(GL_TIME_ELAPSED, gpu_time_query);
    TEST_OPENGL_ERROR();
    // one instance per shell, the vertex shader uses gl_InstanceID
    glDrawElementsInstanced(GL_TRIANGLES, bunny_indices.size(), GL_UNSIGNED_INT,
                            0, shell_count);
    TEST_OPENGL_ERROR();
    glEndQuery(GL_TIME_ELAPSED);
    TEST_OPENGL_ERROR();
    glBindVertexArray(0);
    TEST_OPENGL_ERROR();
    glutSwapBuffers();

    // waits for the GPU to finish the draw call (simple, but it syncs)
    GLuint64 elapsed_ns;
    glGetQueryObjectui64v(gpu_time_query, GL_QUERY_RESULT, &elapsed_ns);
    TEST_OPENGL_ERROR();
    gpu_time_ms += elapsed_ns / 1e6;
    frame_count++;
}

void idle()
{
    // the animation follows the real time, whatever the FPS
    int now = glutGet(GLUT_ELAPSED_TIME);
    anim_time = now / 1000.0f;

    if (now - fps_start_time >= 1000)
    {
        std::cout << "FPS: " << frame_count
                  << ", fur draw: " << gpu_time_ms / frame_count << " ms"
                  << std::endl;
        frame_count = 0;
        gpu_time_ms = 0.0;
        fps_start_time = now;
    }
    glutPostRedisplay();
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
    glutIdleFunc(idle);
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
    std::cout << "GPU: " << glGetString(GL_RENDERER) << std::endl;
    glPointSize(0.1f);
    TEST_OPENGL_ERROR();
    glEnable(GL_DEPTH_TEST);
    TEST_OPENGL_ERROR();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    TEST_OPENGL_ERROR();
    glEnable(GL_CULL_FACE);
    TEST_OPENGL_ERROR();
    glClearColor(0.15, 0.16, 0.18, 1.0);
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
        glBufferData(GL_ARRAY_BUFFER, bunny_positions.size() * sizeof(float),
                     bunny_positions.data(), GL_STATIC_DRAW);
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
        glBufferData(GL_ARRAY_BUFFER, bunny_normals.size() * sizeof(float),
                     bunny_normals.data(), GL_STATIC_DRAW);
        TEST_OPENGL_ERROR();
        glVertexAttribPointer(normal_smooth_location, 3, GL_FLOAT, GL_FALSE, 0,
                              0);
        TEST_OPENGL_ERROR();
        glEnableVertexAttribArray(normal_smooth_location);
        TEST_OPENGL_ERROR();
    }

    // the index buffer is remembered by the VAO
    GLuint index_buffer_id;
    glGenBuffers(1, &index_buffer_id);
    TEST_OPENGL_ERROR();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_id);
    TEST_OPENGL_ERROR();
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, bunny_indices.size() * sizeof(GLuint),
                 bunny_indices.data(), GL_STATIC_DRAW);
    TEST_OPENGL_ERROR();

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

// compiles one shader and prints its errors
GLuint compile_shader(GLenum type, const std::string& filename)
{
    std::string source = load(filename);
    const char* source_ptr = source.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source_ptr, 0);
    glCompileShader(shader);
    TEST_OPENGL_ERROR();
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE)
    {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << filename << ": " << log << std::endl;
    }
    return shader;
}

// links a program from a vertex shader, an optional geometry shader (empty
// name for none) and a fragment shader
GLuint create_program(const std::string& vertex, const std::string& geometry,
                      const std::string& fragment)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, compile_shader(GL_VERTEX_SHADER, vertex));
    if (!geometry.empty())
        glAttachShader(program, compile_shader(GL_GEOMETRY_SHADER, geometry));
    glAttachShader(program, compile_shader(GL_FRAGMENT_SHADER, fragment));
    glLinkProgram(program);
    TEST_OPENGL_ERROR();
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE)
    {
        char log[4096];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "link: " << log << std::endl;
    }
    return program;
}

int main(int argc, char* argv[])
{
    init_glut(argc, argv);
    if (!init_glew())
        std::exit(-1);
    init_GL();
    program_id = create_program("vertex.shd", "", "fragment.shd");
    glUseProgram(program_id);
    glGenQueries(1, &gpu_time_query);
    index_bunny();
    init_object_vbo();
    glutMainLoop();
}
