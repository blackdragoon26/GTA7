#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <cmath>

#define MINIAUDIO_IMPLEMENTATION
#define MA_ENABLE_MP3  // Enable MP3 support
#include "miniaudio.h"

// Audio
ma_engine engine;
ma_sound engineSound;
bool isEngineLoaded = false;
float targetVolume = 0.0f;  // Desired volume (0.0 to 1.0)
float currentVolume = 0.0f; // Smoothly interpolated volume

// Simple Perlin-like noise for terrain
float noise(float x, float z) {
    int xi = (int)x;
    int zi = (int)z;
    float xf = x - xi;
    float zf = z - zi;
    
    // Simple hash-based noise
    auto hash = [](int a, int b) {
        int h = a * 374761393 + b * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h & 0x7fffffff) / (float)0x7fffffff;
    };
    
    float a = hash(xi, zi);
    float b = hash(xi + 1, zi);
    float c = hash(xi, zi + 1);
    float d = hash(xi + 1, zi + 1);
    
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = zf * zf * (3.0f - 2.0f * zf);
    
    return a * (1-u) * (1-v) + b * u * (1-v) + c * (1-u) * v + d * u * v;
}

float getTerrainHeight(float x, float z) {
    float height = 0;
    float scale = 0.03f;
    height += noise(x * scale, z * scale) * 10.0f;
    height += noise(x * scale * 2, z * scale * 2) * 5.0f;
    height += noise(x * scale * 4, z * scale * 4) * 2.5f;
    return height;
}

// Car physics
struct Car {
    glm::vec3 position = glm::vec3(0, 0, 0);
    float rotation = 0.0f;
    float speed = 0.0f;
    float steerAngle = 0.0f;
    
    void update(float dt, bool forward, bool backward, bool left, bool right) {
        const float accel = 15.0f;
        const float brake = 20.0f;
        const float maxSpeed = 20.0f;
        const float friction = 5.0f;
        const float steerSpeed = 2.0f;
        
        // Acceleration
        if (forward) speed += accel * dt;
        if (backward) speed -= brake * dt;
        
        // Friction
        if (!forward && !backward) {
            if (speed > 0) speed -= friction * dt;
            if (speed < 0) speed += friction * dt;
            if (fabs(speed) < 0.1f) speed = 0;
        }
        
        speed = glm::clamp(speed, -maxSpeed * 0.5f, maxSpeed);
        
        // Steering
        if (left) steerAngle = steerSpeed;
        else if (right) steerAngle = -steerSpeed;
        else steerAngle = 0;
        
        // Update rotation and position
        if (fabs(speed) > 0.1f) {
            rotation += steerAngle * dt * (speed / maxSpeed);
        }
        
        position.x += sin(rotation) * speed * dt;
        position.z += cos(rotation) * speed * dt;
        
        // Follow terrain
        position.y = getTerrainHeight(position.x, position.z) + 0.5f;
    }
};

Car car;
int framebufferWidth = 1280;
int framebufferHeight = 720;

// Camera
glm::vec3 cameraPos = glm::vec3(0.0f, 5.0f, 10.0f);
float yaw = -90.0f, pitch = -20.0f;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Chunk settings
const int CHUNK_SIZE = 32;
const float TILE_SIZE = 2.0f;
const int RENDER_DISTANCE = 4;

struct Chunk {
    int x, z;
    unsigned int VAO, VBO, EBO;
    int indexCount;
};

std::map<std::pair<int, int>, Chunk> chunks;

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 Color;
out float Height;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    Color = aColor;
    Height = aPos.y;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 Color;
in float Height;
out vec4 FragColor;

void main() {
    vec3 color = Color;
    // Darken based on height for depth perception
    float factor = 1.0 - (Height / 30.0) * 0.3;
    color *= factor;
    FragColor = vec4(color, 1.0);
}
)";

const char* carVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
out vec3 Color;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 carColor;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    Color = carColor;
}
)";

const char* carFragmentShader = R"(
#version 330 core
in vec3 Color;
out vec4 FragColor;
void main() {
    FragColor = vec4(Color, 1.0);
}
)";

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    
    bool w = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    bool s = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    bool a = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    bool d = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    
    car.update(deltaTime, w, s, a, d);
    // Update engine sound volume based on acceleration
    if (w || s) {
        targetVolume = 0.8f; // Loud when accelerating or reversing
    } else {
        targetVolume = 0.2f; // Idle rumble (optional: set to 0 if you want silence)
    }
}

unsigned int createShaderProgram(const char* vs, const char* fs) {
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vs, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fs, NULL);
    glCompileShader(fragmentShader);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

Chunk createChunk(int chunkX, int chunkZ) {
    Chunk chunk;
    chunk.x = chunkX;
    chunk.z = chunkZ;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (int z = 0; z <= CHUNK_SIZE; z++) {
        for (int x = 0; x <= CHUNK_SIZE; x++) {
            float worldX = (chunkX * CHUNK_SIZE + x) * TILE_SIZE;
            float worldZ = (chunkZ * CHUNK_SIZE + z) * TILE_SIZE;
            float height = getTerrainHeight(worldX, worldZ);
            
            vertices.push_back(worldX);
            vertices.push_back(height);
            vertices.push_back(worldZ);
            
            // Color based on height
            if (height < 2.0f) {
                vertices.push_back(0.8f); vertices.push_back(0.7f); vertices.push_back(0.5f); // Sand
            } else if (height < 8.0f) {
                vertices.push_back(0.3f); vertices.push_back(0.6f); vertices.push_back(0.3f); // Grass
            } else {
                vertices.push_back(0.5f); vertices.push_back(0.5f); vertices.push_back(0.5f); // Rock
            }
        }
    }

    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            int topLeft = z * (CHUNK_SIZE + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * (CHUNK_SIZE + 1) + x;
            int bottomRight = bottomLeft + 1;

            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    chunk.indexCount = indices.size();

    glGenVertexArrays(1, &chunk.VAO);
    glGenBuffers(1, &chunk.VBO);
    glGenBuffers(1, &chunk.EBO);

    glBindVertexArray(chunk.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, chunk.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    return chunk;
}

void updateChunks() {
    int playerChunkX = (int)floor(car.position.x / (CHUNK_SIZE * TILE_SIZE));
    int playerChunkZ = (int)floor(car.position.z / (CHUNK_SIZE * TILE_SIZE));

    for (int z = playerChunkZ - RENDER_DISTANCE; z <= playerChunkZ + RENDER_DISTANCE; z++) {
        for (int x = playerChunkX - RENDER_DISTANCE; x <= playerChunkX + RENDER_DISTANCE; x++) {
            std::pair<int, int> key = {x, z};
            if (chunks.find(key) == chunks.end()) {
                chunks[key] = createChunk(x, z);
            }
        }
    }

    std::vector<std::pair<int, int>> toRemove;
    for (auto& pair : chunks) {
        int dx = abs(pair.first.first - playerChunkX);
        int dz = abs(pair.first.second - playerChunkZ);
        if (dx > RENDER_DISTANCE + 2 || dz > RENDER_DISTANCE + 2) {
            glDeleteVertexArrays(1, &pair.second.VAO);
            glDeleteBuffers(1, &pair.second.VBO);
            glDeleteBuffers(1, &pair.second.EBO);
            toRemove.push_back(pair.first);
        }
    }
    for (auto& key : toRemove) {
        chunks.erase(key);
    }
}

unsigned int createCarVAO() {
    float carVerts[] = {
        // Body
        -1, 0, -2,  1, 0, -2,  1, 1, -2,  -1, 1, -2, // Back
        -1, 0, 2,   1, 0, 2,   1, 1, 2,   -1, 1, 2,  // Front
        -1, 0, -2,  -1, 0, 2,  -1, 1, 2,  -1, 1, -2, // Left
        1, 0, -2,   1, 0, 2,   1, 1, 2,   1, 1, -2,  // Right
        -1, 1, -2,  1, 1, -2,  1, 1, 2,   -1, 1, 2,  // Top
        -1, 0, -2,  1, 0, -2,  1, 0, 2,   -1, 0, 2   // Bottom
    };
    
    unsigned int indices[] = {
        0,1,2, 0,2,3,  4,5,6, 4,6,7,  8,9,10, 8,10,11,
        12,13,14, 12,14,15,  16,17,18, 16,18,19,  20,21,22, 20,22,23
    };
    
    unsigned int VAO, VBO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(carVerts), carVerts, GL_STATIC_DRAW);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    return VAO;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    framebufferWidth = width;
    framebufferHeight = height;
    glViewport(0, 0, width, height);
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Infinite Open World", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD\n";
        return -1;
    }

    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);

    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);


    glEnable(GL_DEPTH_TEST);


    // Initialize audio engine
if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
    std::cout << "Failed to initialize audio engine\n";
} else {
    // Load engine sound (looping)
    if (ma_sound_init_from_file(&engine, "enginesound.mp3", MA_SOUND_FLAG_LOOPING, NULL, NULL, &engineSound) != MA_SUCCESS) {
        std::cout << "Failed to load enginesound.mp3\n";
    } else {
        ma_sound_set_volume(&engineSound, 0.0f); // Start silent
        ma_sound_start(&engineSound);
        isEngineLoaded = true;
    }
}




    unsigned int terrainShader = createShaderProgram(vertexShaderSource, fragmentShaderSource);
    unsigned int carShader = createShaderProgram(carVertexShader, carFragmentShader);
    unsigned int carVAO = createCarVAO();
    
    // Initialize car position
    car.position.y = getTerrainHeight(0, 0) + 0.5f;

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);
        if (isEngineLoaded) {
            // Interpolate toward target volume (adjust 2.0f for faster/slower fade)
            currentVolume += (targetVolume - currentVolume) * (1.0f - expf(-2.0f * deltaTime));
            ma_sound_set_volume(&engineSound, currentVolume);
        }
        updateChunks();

        // Camera follows car
        float camDist = 12.0f;
        float camHeight = 5.0f;
        cameraPos.x = car.position.x - sin(car.rotation) * camDist;
        cameraPos.y = car.position.y + camHeight;
        cameraPos.z = car.position.z - cos(car.rotation) * camDist;
        
        glm::vec3 lookAt = car.position + glm::vec3(0, 1, 0);

        glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


        float aspect = (float)framebufferWidth / (float)framebufferHeight;
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        glm::mat4 view = glm::lookAt(cameraPos, lookAt, glm::vec3(0, 1, 0));

        // Draw terrain
        glUseProgram(terrainShader);
        glUniformMatrix4fv(glGetUniformLocation(terrainShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(terrainShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(terrainShader, "model"), 1, GL_FALSE, glm::value_ptr(model));

        for (auto& pair : chunks) {
            glBindVertexArray(pair.second.VAO);
            glDrawElements(GL_TRIANGLES, pair.second.indexCount, GL_UNSIGNED_INT, 0);
        }

        // Draw car
        glUseProgram(carShader);
        model = glm::mat4(1.0f);
        model = glm::translate(model, car.position);
        model = glm::rotate(model, car.rotation, glm::vec3(0, 1, 0));
        glUniformMatrix4fv(glGetUniformLocation(carShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(carShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(carShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(glGetUniformLocation(carShader, "carColor"), 0.8f, 0.2f, 0.2f);
        
        glBindVertexArray(carVAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();

    if (isEngineLoaded) {
        ma_sound_uninit(&engineSound);
    }
    ma_engine_uninit(&engine);
    return 0;
}