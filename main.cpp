// clang-format off
#include <GL/glew.h> // glew doit etre tout en haut
// clang-format on
#include <GL/freeglut.h>
#include <algorithm>
#include <array>
#include <cmath>
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

GLuint mesh_vao_id;
GLuint fin_vao_id;
GLuint program_id;
GLuint fin_program_id;

// the mesh that gets the fur: the bunny, or a sphere with --ball
std::vector<GLfloat> mesh_positions;
std::vector<GLfloat> mesh_normals;
std::vector<GLuint> mesh_indices;

// the bunny is a triangle soup: a vertex is repeated in every triangle that
// uses it. The identical vertices are merged and drawn with an index buffer,
// so the vertex shader runs about 6 times less.
void load_bunny()
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
        GLuint new_index = mesh_positions.size() / 3;
        // insert does nothing if the vertex is already known
        auto result = vertex_index.insert({ vertex, new_index });
        mesh_indices.push_back(result.first->second);
        if (result.second)
        {
            mesh_positions.insert(mesh_positions.end(), vertex.begin(),
                                  vertex.begin() + 3);
            mesh_normals.insert(mesh_normals.end(), vertex.begin() + 3,
                                vertex.end());
        }
    }
    std::cout << "bunny: " << mesh_indices.size() << " vertices, "
              << mesh_positions.size() / 3 << " unique" << std::endl;
}

// a sphere centered like the bunny (center (0, 0.5, 0)): a vertex on each
// pole and rings of vertices in between. The last vertex of a
// ring is linked to the first one (no copy on the seam), so the mesh is
// closed: every edge has 2 triangles, as the fins need.
void make_sphere(int rings, int segments, float radius)
{
    const float pi = 3.14159265f;
    // direction from the center, which is also the normal
    auto add_vertex = [radius](float x, float y, float z) {
        mesh_positions.insert(mesh_positions.end(),
                              { radius * x, 0.5f + radius * y, radius * z });
        mesh_normals.insert(mesh_normals.end(), { x, y, z });
    };
    auto add_triangle = [](GLuint a, GLuint b, GLuint c) {
        mesh_indices.insert(mesh_indices.end(), { a, b, c });
    };
    // index of the vertex j of the ring i (rings 1 to rings - 1)
    auto ring_vertex = [segments](int i, int j) {
        return GLuint(1 + (i - 1) * segments + j % segments);
    };

    add_vertex(0.0f, 1.0f, 0.0f);
    for (int i = 1; i < rings; i++)
    {
        float theta = pi * i / rings; // angle from the north pole
        for (int j = 0; j < segments; j++)
        {
            float phi = 2.0f * pi * j / segments;
            add_vertex(std::sin(theta) * std::cos(phi), std::cos(theta),
                       std::sin(theta) * std::sin(phi));
        }
    }
    add_vertex(0.0f, -1.0f, 0.0f);
    GLuint south = mesh_positions.size() / 3 - 1;

    // counter clockwise seen from outside, so the back faces are culled
    for (int j = 0; j < segments; j++)
        add_triangle(0, ring_vertex(1, j + 1), ring_vertex(1, j));
    for (int i = 1; i < rings - 1; i++)
    {
        for (int j = 0; j < segments; j++)
        {
            GLuint a = ring_vertex(i, j);
            GLuint b = ring_vertex(i, j + 1);
            GLuint c = ring_vertex(i + 1, j);
            GLuint d = ring_vertex(i + 1, j + 1);
            add_triangle(a, d, c);
            add_triangle(a, b, d);
        }
    }
    for (int j = 0; j < segments; j++)
        add_triangle(ring_vertex(rings - 1, j), ring_vertex(rings - 1, j + 1),
                     south);
    std::cout << "ball: " << mesh_positions.size() / 3 << " vertices, "
              << mesh_indices.size() / 3 << " triangles" << std::endl;
}

// the edges of the mesh for the fins, 4 indices per edge for
// GL_LINES_ADJACENCY: the opposite vertex of the 1st triangle, the 2 vertices
// of the edge, the opposite vertex of the 2nd triangle
std::vector<GLuint> edge_indices;

void find_edges()
{
    // edge (smallest index, biggest index) -> opposite vertices
    std::map<std::pair<GLuint, GLuint>, std::vector<GLuint>> opposite;
    for (size_t i = 0; i < mesh_indices.size(); i += 3)
    {
        for (int k = 0; k < 3; k++)
        {
            GLuint a = mesh_indices[i + k];
            GLuint b = mesh_indices[i + (k + 1) % 3];
            GLuint c = mesh_indices[i + (k + 2) % 3];
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

// angle of the model around the vertical axis, turned with the arrows
float rotation = 0.0f;
const float rotation_speed = 2.0f; // radians per second
bool left_held = false;
bool right_held = false;
int last_frame_time = 0;

// FPS counter and GPU time of the shells and the fins, printed every second
int frame_count = 0;
int fps_start_time = 0;
GLuint shell_time_query;
GLuint fin_time_query;
double shell_time_ms = 0.0;
double fin_time_ms = 0.0;

void update_uniforms(GLuint program)
{
    glUniform1f(glGetUniformLocation(program, "rotation"), rotation);
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
    glBindVertexArray(mesh_vao_id);
    // the GPU measures how long the draw call takes on its side
    glBeginQuery(GL_TIME_ELAPSED, shell_time_query);
    glDrawElementsInstanced(GL_TRIANGLES, mesh_indices.size(), GL_UNSIGNED_INT,
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
    // the rotation follows the real time, whatever the FPS
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - last_frame_time) / 1000.0f;
    last_frame_time = now;
    if (right_held)
        rotation += rotation_speed * dt;
    if (left_held)
        rotation -= rotation_speed * dt;

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

// the model turns while an arrow is held down
void arrow(int key, bool held)
{
    if (key == GLUT_KEY_LEFT)
        left_held = held;
    else if (key == GLUT_KEY_RIGHT)
        right_held = held;
}

void arrow_down(int key, int, int)
{
    arrow(key, true);
}

void arrow_up(int key, int, int)
{
    arrow(key, false);
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
    glutSpecialFunc(arrow_down);
    glutSpecialUpFunc(arrow_up);
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
    GLuint position_vbo = create_buffer(mesh_positions.size() * sizeof(GLfloat),
                                        mesh_positions.data());
    GLuint normal_vbo = create_buffer(mesh_normals.size() * sizeof(GLfloat),
                                      mesh_normals.data());
    GLuint triangle_ibo = create_buffer(mesh_indices.size() * sizeof(GLuint),
                                        mesh_indices.data());
    GLuint edge_ibo = create_buffer(edge_indices.size() * sizeof(GLuint),
                                    edge_indices.data());
    mesh_vao_id = create_vao(position_vbo, normal_vbo, triangle_ibo);
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
    // ./main --ball: a furry ball instead of the bunny
    bool ball = false;
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--ball")
            ball = true;
    if (ball)
    {
        // a small ball with long fur: mostly a clump of strands
        make_sphere(64, 128, 0.12f);
        fur_length = 0.28f;
        shell_count = 128;
        // more strands, so they are thinner: long strands on a small ball
        // get wider toward the tip (the shells are bigger than the skin)
        strand_density = 400.0f;
    }
    else
        load_bunny();
    find_edges();
    init_object_vbo();
    glutMainLoop();
}
