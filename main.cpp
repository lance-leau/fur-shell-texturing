// clang-format off
#include <GL/glew.h> // glew doit etre tout en haut
// clang-format on
#include <GL/freeglut.h>
#include <algorithm>
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
GLuint fin_vao_id;
GLuint program_id;
GLuint fin_program_id;

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

// the edges of the bunny for the fins, 4 indices per edge for
// GL_LINES_ADJACENCY: the opposite vertex of the 1st triangle, the 2 vertices
// of the edge, the opposite vertex of the 2nd triangle
std::vector<GLuint> edge_indices;

void find_edges()
{
    // edge (smallest index, biggest index) -> opposite vertices
    std::map<std::pair<GLuint, GLuint>, std::vector<GLuint>> opposite;
    for (size_t i = 0; i < bunny_indices.size(); i += 3)
    {
        for (int k = 0; k < 3; k++)
        {
            GLuint a = bunny_indices[i + k];
            GLuint b = bunny_indices[i + (k + 1) % 3];
            GLuint c = bunny_indices[i + (k + 2) % 3];
            opposite[{ std::min(a, b), std::max(a, b) }].push_back(c);
        }
    }
    for (auto& edge : opposite)
    {
        // an edge on a hole of the mesh has only one triangle: no fin
        if (edge.second.size() != 2)
            continue;
        edge_indices.push_back(edge.second[0]);
        edge_indices.push_back(edge.first.first);
        edge_indices.push_back(edge.first.second);
        edge_indices.push_back(edge.second[1]);
    }
    std::cout << "fins: " << edge_indices.size() / 4 << " edges" << std::endl;
}

// fur parameters
int shell_count = 64;
float fur_length = 0.05f;
float strand_density = 250.0f;
bool show_fins = true;

float anim_time = 0.0f;

// FPS counter and GPU time of the shells and the fins, printed every second
int frame_count = 0;
int fps_start_time = 0;
GLuint shell_time_query;
GLuint fin_time_query;
double shell_time_ms = 0.0;
double fin_time_ms = 0.0;

void update_uniforms(GLuint program)
{
    glUniform1f(glGetUniformLocation(program, "anim_time"), anim_time);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(program, "strand_density"),
                strand_density);
    TEST_OPENGL_ERROR();
    glUniform1i(glGetUniformLocation(program, "shell_count"), shell_count);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(program, "fur_length"), fur_length);
    TEST_OPENGL_ERROR();
}

// waits for the GPU to give the time of a query, in ms (simple, but it syncs)
double query_time_ms(GLuint query)
{
    GLuint64 elapsed_ns;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed_ns);
    TEST_OPENGL_ERROR();
    return elapsed_ns / 1e6;
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

    // shells: one instance per shell, the vertex shader uses gl_InstanceID
    glUseProgram(program_id);
    update_uniforms(program_id);
    glBindVertexArray(bunny_vao_id);
    // the GPU measures how long the draw call takes on its side
    glBeginQuery(GL_TIME_ELAPSED, shell_time_query);
    glDrawElementsInstanced(GL_TRIANGLES, bunny_indices.size(), GL_UNSIGNED_INT,
                            0, shell_count);
    glEndQuery(GL_TIME_ELAPSED);
    TEST_OPENGL_ERROR();

    // fins: the geometry shader gets each edge with its 2 triangles and
    // builds a fin on the edges of the silhouette
    if (show_fins)
    {
        glUseProgram(fin_program_id);
        update_uniforms(fin_program_id);
        glBindVertexArray(fin_vao_id);
        // a fin can be seen from both sides
        glDisable(GL_CULL_FACE);
        glBeginQuery(GL_TIME_ELAPSED, fin_time_query);
        glDrawElements(GL_LINES_ADJACENCY, edge_indices.size(), GL_UNSIGNED_INT,
                       0);
        glEndQuery(GL_TIME_ELAPSED);
        glEnable(GL_CULL_FACE);
        TEST_OPENGL_ERROR();
    }

    glBindVertexArray(0);
    glutSwapBuffers();

    shell_time_ms += query_time_ms(shell_time_query);
    if (show_fins)
        fin_time_ms += query_time_ms(fin_time_query);
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
                  << ", shells: " << shell_time_ms / frame_count
                  << " ms, fins: " << fin_time_ms / frame_count << " ms"
                  << std::endl;
        frame_count = 0;
        shell_time_ms = 0.0;
        fin_time_ms = 0.0;
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
    else if (key == 'f')
        show_fins = !show_fins;
    else
        return;
    std::cout << "shells: " << shell_count << ", length: " << fur_length
              << ", density: " << strand_density
              << ", fins: " << (show_fins ? "on" : "off") << std::endl;
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

// a buffer is only memory: the target used to fill it does not matter
GLuint create_buffer(size_t size, const void* data)
{
    GLuint buffer_id;
    glGenBuffers(1, &buffer_id);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    TEST_OPENGL_ERROR();
    return buffer_id;
}

// a VAO with the positions (location 0), the normals (location 1) and the
// given index buffer (remembered by the VAO)
GLuint create_vao(GLuint position_vbo, GLuint normal_vbo, GLuint index_buffer)
{
    GLuint vao_id;
    glGenVertexArrays(1, &vao_id);
    glBindVertexArray(vao_id);
    glBindBuffer(GL_ARRAY_BUFFER, position_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, normal_vbo);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
    glBindVertexArray(0);
    TEST_OPENGL_ERROR();
    return vao_id;
}

// the shells and the fins use the same vertices, with different indices
void init_object_vbo()
{
    GLuint position_vbo = create_buffer(
        bunny_positions.size() * sizeof(GLfloat), bunny_positions.data());
    GLuint normal_vbo = create_buffer(bunny_normals.size() * sizeof(GLfloat),
                                      bunny_normals.data());
    GLuint triangle_ibo = create_buffer(bunny_indices.size() * sizeof(GLuint),
                                        bunny_indices.data());
    GLuint edge_ibo = create_buffer(edge_indices.size() * sizeof(GLuint),
                                    edge_indices.data());
    bunny_vao_id = create_vao(position_vbo, normal_vbo, triangle_ibo);
    fin_vao_id = create_vao(position_vbo, normal_vbo, edge_ibo);
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
    fin_program_id =
        create_program("vertex.shd", "fin_geometry.shd", "fin_fragment.shd");
    glGenQueries(1, &shell_time_query);
    glGenQueries(1, &fin_time_query);
    index_bunny();
    find_edges();
    init_object_vbo();
    glutMainLoop();
}
