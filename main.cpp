#include <GL/glew.h>

#include <GL/freeglut.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "bunny.hh"

#define TEST_OPENGL_ERROR()                                                    \
    do                                                                         \
    {                                                                          \
        GLenum err = glGetError();                                             \
        if (err != GL_NO_ERROR)                                                \
            std::cerr << "OpenGL ERROR! " << __LINE__ << std::endl;            \
    } while (0)

// all the IDs
GLuint mesh_vao_id;
GLuint fin_vao_id;
GLuint program_id;
GLuint fin_program_id;
GLuint physics_program_id;

// bunny arrays from the .hh file
std::vector<GLfloat> mesh_positions;
std::vector<GLfloat> mesh_normals;
std::vector<GLuint> mesh_indices;

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
        if (vertex_index.count(vertex) == 0)
        {
            vertex_index[vertex] = mesh_positions.size() / 3;
            mesh_positions.insert(mesh_positions.end(), vertex.begin(),
                                  vertex.begin() + 3);
            mesh_normals.insert(mesh_normals.end(), vertex.begin() + 3,
                                vertex.end());
        }
        mesh_indices.push_back(vertex_index[vertex]);
    }
    // std::cout << "bunny: " << mesh_indices.size() << " vertices, "
    //           << mesh_positions.size() / 3 << " unique" << std::endl;
}

void make_sphere(int rings, int segments, float radius)
{
    const float pi = 3.14159265f;
    for (int i = 0; i <= rings; i++)
    {
        for (int j = 0; j < segments; j++)
        {
            float theta = pi * i / rings;
            float phi = 2.0f * pi * j / segments;
            float x = std::sin(theta) * std::cos(phi);
            float y = std::cos(theta);
            float z = std::sin(theta) * std::sin(phi);
            mesh_positions.insert(
                mesh_positions.end(),
                { radius * x, 0.5f + radius * y, radius * z });
            mesh_normals.insert(mesh_normals.end(), { x, y, z });
        }
    }
    for (int i = 0; i < rings; i++)
    {
        for (int j = 0; j < segments; j++)
        {
            GLuint a = i * segments + j;
            GLuint b = i * segments + (j + 1) % segments;
            GLuint c = a + segments;
            GLuint d = b + segments;
            if (i > 0)
                mesh_indices.insert(mesh_indices.end(), { a, b, d });
            if (i < rings - 1)
                mesh_indices.insert(mesh_indices.end(), { a, d, c });
        }
    }
}

std::vector<GLuint> edge_indices;

void find_edges()
{
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
        if (edge.second.size() != 2)
            continue;
        edge_indices.push_back(edge.second[0]);
        edge_indices.push_back(edge.first.first);
        edge_indices.push_back(edge.first.second);
        edge_indices.push_back(edge.second[1]);
    }
    // std::cout << "fins: " << edge_indices.size() / 4 << " edges" <<
    // std::endl;
}

// global var
int shell_count = 64;
float fur_length = 0.05f;
float strand_density = 250.0f;
bool show_fins = true;
const float spring_strength = 1.2f;
const float damping_ratio = 0.6f;
float windStrength = 16.0f;
float physics_time = 0.0f;
const float physics_step = 1.0f / 240.0f;
float physics_time_left = 0.0f;
float frame_dt = 0.0f;
float rotation = 0.0f;
const float rotation_speed = 2.0f;
const float growth_speed = 0.7f;
int last_frame_time = 0;
bool swinging = false;
float swing_time = 0.0f;
const float swing_width = 0.35f;
const float swing_period = 2.0f;
// kb controls
bool left_held = false;
bool right_held = false;
bool up_held = false;
bool down_held = false;

void set_model_matrix(GLuint program)
{
    float c = std::cos(rotation);
    float s = std::sin(rotation);
    float x =
        swing_width * std::sin(2.0f * 3.14159265f * swing_time / swing_period);
    GLfloat model[16] = { c,    0.0f, -s,           0.0f,  0.0f,
                          1.0f, 0.0f, 0.0f,         s,     0.0f,
                          c,    0.0f, 0.57735f * x, -0.5f, -0.57735f * x,
                          1.0f };
    glUniformMatrix4fv(glGetUniformLocation(program, "model_matrix"), 1,
                       GL_FALSE, model);
    TEST_OPENGL_ERROR();
}

void update_uniforms(GLuint program)
{
    set_model_matrix(program);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(program, "strand_density"),
                strand_density);
    TEST_OPENGL_ERROR();
    glUniform1i(glGetUniformLocation(program, "shell_count"), shell_count);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(program, "fur_length"), fur_length);
    TEST_OPENGL_ERROR();
}

void window_resize(int width, int height)
{
    glViewport(0, 0, width, height);
    TEST_OPENGL_ERROR();
}

void simulate()
{
    glUseProgram(physics_program_id);
    set_model_matrix(physics_program_id);
    GLuint vertex_count = mesh_positions.size() / 3;
    glUniform1ui(glGetUniformLocation(physics_program_id, "vertex_count"),
                 vertex_count);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(physics_program_id, "fur_length"),
                fur_length);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(physics_program_id, "dt"), physics_step);
    float stifness = spring_strength * 9.81f / fur_length;
    float damping =
        std::exp(-2.0f * damping_ratio * std::sqrt(stifness) * physics_step);
    glUniform1f(glGetUniformLocation(physics_program_id, "stifness"), stifness);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(physics_program_id, "damping"), damping);
    TEST_OPENGL_ERROR();
    glUniform1f(glGetUniformLocation(physics_program_id, "windStrength"),
                windStrength);
    TEST_OPENGL_ERROR();

    physics_time_left += frame_dt;
    while (physics_time_left >= physics_step)
    {
        glUniform1f(glGetUniformLocation(physics_program_id, "time"),
                    physics_time);
        glDispatchCompute((vertex_count + 63) / 64, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        physics_time_left -= physics_step;
        physics_time += physics_step;
    }
    TEST_OPENGL_ERROR();
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    TEST_OPENGL_ERROR();

    simulate();

    glUseProgram(program_id);
    TEST_OPENGL_ERROR();
    update_uniforms(program_id);
    TEST_OPENGL_ERROR();
    glBindVertexArray(mesh_vao_id);
    TEST_OPENGL_ERROR();
    glDrawElementsInstanced(GL_TRIANGLES, mesh_indices.size(), GL_UNSIGNED_INT,
                            0, shell_count);
    TEST_OPENGL_ERROR();

    if (show_fins)
    {
        glUseProgram(fin_program_id);
        TEST_OPENGL_ERROR();
        update_uniforms(fin_program_id);
        TEST_OPENGL_ERROR();
        glBindVertexArray(fin_vao_id);
        TEST_OPENGL_ERROR();
        glDisable(GL_CULL_FACE);
        TEST_OPENGL_ERROR();
        glDrawElements(GL_LINES_ADJACENCY, edge_indices.size(), GL_UNSIGNED_INT,
                       0);
        TEST_OPENGL_ERROR();
        glEnable(GL_CULL_FACE);
        TEST_OPENGL_ERROR();
    }

    glBindVertexArray(0);
    TEST_OPENGL_ERROR();
    glutSwapBuffers();
    TEST_OPENGL_ERROR();
}

void idle()
{
    int now = glutGet(GLUT_ELAPSED_TIME);
    frame_dt = (now - last_frame_time) / 1000.0f;
    last_frame_time = now;
    if (right_held)
        rotation += rotation_speed * frame_dt;
    if (left_held)
        rotation -= rotation_speed * frame_dt;
    if (up_held)
        fur_length *= std::exp(growth_speed * frame_dt);
    if (down_held && fur_length > 0.01f)
        fur_length *= std::exp(-growth_speed * frame_dt);
    if (swinging)
        swing_time += frame_dt;
    // std::cout << "FPS: " << frame_count
    //           << ", physics: " << physics_time_ms / frame_count
    //           << " ms, shells: " << shell_time_ms / frame_count
    //           << " ms, fins: " << fin_time_ms / frame_count << " ms"
    //           << std::endl;
    glutPostRedisplay();
}

void keyboard(unsigned char key, int, int)
{
    if (key == '+')
        shell_count++;
    else if (key == '-' && shell_count > 2)
        shell_count--;
    else if (key == 'D')
        strand_density *= 1.1f;
    else if (key == 'd')
        strand_density /= 1.1f;
    else if (key == 'f')
        show_fins = !show_fins;
    else if (key == 't')
        swinging = !swinging;
    else if (key == ',' && windStrength > 0.0f)
        windStrength -= 2.0f;
    else if (key == '.')
        windStrength += 2.0f;
    // std::cout << "shells: " << shell_count << ", length: " << fur_length
    //           << ", density: " << strand_density
    //           << ", fins: " << (show_fins ? "on" : "off")
    //           << ", swing: " << (swinging ? "on" : "off")
    //           << ", wind: " << (wind_on ? "on" : "off") << " (" <<
    //           windStrength
    //           << ")" << std::endl;
}

void arrow(int key, bool held)
{
    if (key == GLUT_KEY_LEFT)
        left_held = held;
    else if (key == GLUT_KEY_RIGHT)
        right_held = held;
    else if (key == GLUT_KEY_UP)
        up_held = held;
    else if (key == GLUT_KEY_DOWN)
        down_held = held;
    // if ((key == GLUT_KEY_UP || key == GLUT_KEY_DOWN) && !held)
    //     std::cout << "length: " << fur_length << std::endl;
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
    // std::cout << "GPU: " << glGetString(GL_RENDERER) << std::endl;
    glEnable(GL_DEPTH_TEST);
    TEST_OPENGL_ERROR();
    glEnable(GL_CULL_FACE);
    TEST_OPENGL_ERROR();
    glClearColor(0.15, 0.16, 0.18, 1.0);
    TEST_OPENGL_ERROR();
}

GLuint create_buffer(size_t size, const void* data)
{
    GLuint buffer_id;
    glGenBuffers(1, &buffer_id);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_id);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    TEST_OPENGL_ERROR();
    return buffer_id;
}

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

    std::vector<GLfloat> strands;
    for (size_t i = 0; i < mesh_positions.size(); i += 3)
    {
        float x = mesh_positions[i] + mesh_normals[i] * fur_length;
        float y = mesh_positions[i + 1] + mesh_normals[i + 1] * fur_length;
        float z = mesh_positions[i + 2] + mesh_normals[i + 2] * fur_length;
        strands.insert(strands.end(),
                       { x, y - 0.5f, z, 0.0f, x, y - 0.5f, z, 0.0f });
    }
    GLuint strand_ssbo =
        create_buffer(strands.size() * sizeof(GLfloat), strands.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, position_vbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, normal_vbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, strand_ssbo);
    TEST_OPENGL_ERROR();
}

std::string load(const std::string& filename)
{
    std::ifstream file(filename);
    std::stringstream content;
    content << file.rdbuf();
    return content.str();
}

void add_shader(GLuint program, GLenum type, const std::string& filename)
{
    std::string source = load(filename);
    const char* source_ptr = source.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source_ptr, 0);
    glCompileShader(shader);
    char log[4096] = "";
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::cerr << log;
    glAttachShader(program, shader);
    TEST_OPENGL_ERROR();
}

void link_program(GLuint program)
{
    glLinkProgram(program);
    char log[4096] = "";
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    std::cerr << log;
    TEST_OPENGL_ERROR();
}

int main(int argc, char* argv[])
{
    init_glut(argc, argv);
    if (!init_glew())
        std::exit(-1);
    init_GL();

    program_id = glCreateProgram();
    add_shader(program_id, GL_VERTEX_SHADER, "vertex.shd");
    add_shader(program_id, GL_FRAGMENT_SHADER, "fragment.shd");
    link_program(program_id);

    fin_program_id = glCreateProgram();
    add_shader(fin_program_id, GL_VERTEX_SHADER, "vertex.shd");
    add_shader(fin_program_id, GL_GEOMETRY_SHADER, "fin_geometry.shd");
    add_shader(fin_program_id, GL_FRAGMENT_SHADER, "fin_fragment.shd");
    link_program(fin_program_id);

    physics_program_id = glCreateProgram();
    add_shader(physics_program_id, GL_COMPUTE_SHADER, "physics.shd");
    link_program(physics_program_id);

    if (argc > 1 && std::string(argv[1]) == "--ball")
    {
        make_sphere(64, 128, 0.12f);
        fur_length = 0.28f;
        shell_count = 192;
        strand_density = 400.0f;
    }
    else
        load_bunny();
    find_edges();
    init_object_vbo();
    glutMainLoop();
}
