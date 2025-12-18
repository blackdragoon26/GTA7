#include <algorithm>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <random>

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif


#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_ENABLE_MP3
#include "miniaudio.h"


// ============ FORWARD DECLARATIONS (BEFORE STRUCTS) ============

enum TerrainType {
    TERRAIN_ROAD,
    TERRAIN_GRASS,
    TERRAIN_DIRT,
    TERRAIN_PUDDLE
};

struct TerrainInfo {
    float height;
    TerrainType type;
};

struct Puddle {
    glm::vec2 pos;
    float radius;
};

struct Bullet {
    glm::vec3 pos;
    glm::vec3 vel;
    float lifetime;
};

struct Building {
    glm::vec3 position;
    float width, depth, height;
};


float noise(float x, float z);
float getTerrainHeight(float x, float z);
TerrainInfo getTerrainInfo(float x, float z);

// ============ GLOBAL VECTORS (MUST BE BEFORE Car STRUCT) ============
std::vector<Building> buildings;
std::vector<Puddle> puddles;
std::vector<Bullet> bullets;

// Random generator
std::random_device rd;
std::mt19937 gen(rd());

// ============ NOW CAR STRUCT CAN USE THEM ============
struct Car {
    glm::vec3 position = glm::vec3(0, 0, 0);
    float rotation = 0.0f;
    float speed = 0.0f;
    float steerAngle = 0.0f;
    float driftAngle = 0.0f;
    bool isDrifting = false;
    
    void update(float dt, bool forward, bool backward, bool left, bool right, bool drift) {
        const float accel = 18.0f;
        const float brake = 25.0f;
        const float maxSpeed = 25.0f;
        const float friction = 4.0f;
        
        TerrainInfo info = getTerrainInfo(position.x, position.z);
        position.y = info.height + 0.5f;

        float speedMult = 1.0f;
        float steerMult = 1.0f;

        switch (info.type) {
            case TERRAIN_ROAD:
                speedMult = 1.5f;   // 50% faster
                steerMult = 1.2f;
                break;
            case TERRAIN_GRASS:
                speedMult = 0.5f;   // 50% speed - you'll feel this
                steerMult = 0.7f;
                break;
            case TERRAIN_DIRT:
                speedMult = 0.3f;   // 30% speed - very slow
                steerMult = 0.5f;
                break;
            case TERRAIN_PUDDLE:
                speedMult = 0.1f;   // 10% speed - almost stuck
                steerMult = 0.15f;  // barely steerable
                drift = true;
                break;
        }

        static float debugTimer = 0.0f;
        static TerrainType lastType = TERRAIN_ROAD;
        debugTimer += dt;

        if (info.type != lastType || debugTimer > 1.0f) {
            const char* terrainName[] = {"ROAD", "GRASS", "DIRT", "PUDDLE"};
            std::cout << "Terrain: " << terrainName[info.type] 
                    << " | Speed Mult: " << speedMult 
                    << " | Current Speed: " << speed << "\n";
            lastType = info.type;
            debugTimer = 0.0f;
        }

        isDrifting = drift;  // Set drift state
        const float steerSpeed = (drift ? 3.5f : 2.2f) * steerMult;

        if (forward) speed += accel * speedMult * dt;
        if (backward) speed -= brake * speedMult * dt;

        // Apply terrain-based friction (always, not just when coasting)
        float terrainFriction = friction * (2.0f - speedMult); 

        if (!forward && !backward) {
            if (speed > 0) speed -= terrainFriction * dt;
            if (speed < 0) speed += terrainFriction * dt;
            if (std::fabs(speed) < 0.1f) speed = 0;
        }

        // CRITICAL FIX: Terrain-based max speed
        float terrainMaxSpeed = maxSpeed * speedMult;
        speed = glm::clamp(speed, -terrainMaxSpeed * 0.5f, terrainMaxSpeed);

        // CRITICAL FIX: Active speed reduction when over terrain limit
        if (speed > terrainMaxSpeed) {
            speed -= (speed - terrainMaxSpeed) * 5.0f * dt; // Quick slowdown
        }
        if (speed < -terrainMaxSpeed * 0.5f) {
            speed -= (speed + terrainMaxSpeed * 0.5f) * 5.0f * dt;
        }



        if (left) steerAngle = steerSpeed;
        else if (right) steerAngle = -steerSpeed;
        else steerAngle = 0;

        if (std::fabs(speed) > 0.1f) {
            float turnRate = isDrifting ? 0.7f : 1.0f;
            rotation += steerAngle * dt * (speed / maxSpeed) * turnRate;
        }

        if (isDrifting) {
            driftAngle += (steerAngle * 0.3f - driftAngle) * 5.0f * dt;
        } else {
            driftAngle *= 0.9f;
        }

        // Store old position
        glm::vec3 oldPosition = position;

        // Calculate new position
        position.x += std::sin(rotation) * speed * dt;
        position.z += std::cos(rotation) * speed * dt;

        // === BUILDING COLLISION (with car size buffer) ===
        const float carRadius = 2.5f; // Car's collision radius

        bool collided = false;

        for (const auto& b : buildings) {
            // Expanded collision box that accounts for car size
            float minX = b.position.x - b.width/2 - carRadius;
            float maxX = b.position.x + b.width/2 + carRadius;
            float minZ = b.position.z - b.depth/2 - carRadius;
            float maxZ = b.position.z + b.depth/2 + carRadius;
            
            // Check if car is in expanded collision zone
            if (position.x >= minX && position.x <= maxX &&
                position.z >= minZ && position.z <= maxZ) {
                
                // REVERT to old position completely
                position = oldPosition;
                
                // Stop the car
                speed *= 0.2f;
                
                collided = true;
                std::cout << "BUMPED INTO BUILDING! (at " 
                        << b.position.x << ", " << b.position.z << ")\n";
                break;
            }
        }

        // If we collided, try to slide along the wall instead of full stop
        if (collided) {
            // Try moving only in X direction
            glm::vec3 slideX = oldPosition;
            slideX.x += std::sin(rotation) * speed * dt;
            
            bool canSlideX = true;
            for (const auto& b : buildings) {
                float minX = b.position.x - b.width/2 - carRadius;
                float maxX = b.position.x + b.width/2 + carRadius;
                float minZ = b.position.z - b.depth/2 - carRadius;
                float maxZ = b.position.z + b.depth/2 + carRadius;
                
                if (slideX.x >= minX && slideX.x <= maxX &&
                    slideX.z >= minZ && slideX.z <= maxZ) {
                    canSlideX = false;
                    break;
                }
            }
            
            // Try moving only in Z direction
            glm::vec3 slideZ = oldPosition;
            slideZ.z += std::cos(rotation) * speed * dt;
            
            bool canSlideZ = true;
            for (const auto& b : buildings) {
                float minX = b.position.x - b.width/2 - carRadius;
                float maxX = b.position.x + b.width/2 + carRadius;
                float minZ = b.position.z - b.depth/2 - carRadius;
                float maxZ = b.position.z + b.depth/2 + carRadius;
                
                if (slideZ.x >= minX && slideZ.x <= maxX &&
                    slideZ.z >= minZ && slideZ.z <= maxZ) {
                    canSlideZ = false;
                    break;
                }
            }
            
            // Apply sliding if possible
            if (canSlideX) position.x = slideX.x;
            if (canSlideZ) position.z = slideZ.z;
        }

    }     
};

struct PoliceCar {
    glm::vec3 position;
    float rotation;
    float speed;
    
    void update(float dt, glm::vec3 targetPos) {
        glm::vec3 toTarget = targetPos - position;
        float dist = glm::length(toTarget);
        
        if (dist > 1.0f) {
            toTarget = glm::normalize(toTarget);
            
            float targetRot = atan2(toTarget.x, toTarget.z);
            float rotDiff = targetRot - rotation;
            
            while (rotDiff > M_PI) rotDiff -= 2 * M_PI;
            while (rotDiff < -M_PI) rotDiff += 2 * M_PI;
            
            rotation += rotDiff * 3.0f * dt;
            
            float targetSpeed = dist > 30.0f ? 18.0f : 12.0f;
            speed += (targetSpeed - speed) * 2.0f * dt;
        }
        
        position.x += sin(rotation) * speed * dt;
        position.z += cos(rotation) * speed * dt;
        position.y = getTerrainHeight(position.x, position.z) + 0.5f;
    }
};

std::vector<PoliceCar> policeCars;

// Audio
ma_engine engine;
ma_sound engineSound;
bool isEngineLoaded = false;
float targetVolume = 0.0f;
float currentVolume = 0.0f;

Car car;

// Game state
float survivalTime = 0.0f;
float highScore = 0.0f;
bool gameStarted = false;
float spawnTimer = 0.0f;
float shootTimer = 0.0f;

int framebufferWidth = 1280;
int framebufferHeight = 720;
glm::vec3 cameraPos = glm::vec3(0.0f, 5.0f, 10.0f);
float deltaTime = 0.0f;

float fogDensity = 0.02f;
glm::vec3 fogColor = glm::vec3(0.7f, 0.75f, 0.8f);

// ============ FUNCTION IMPLEMENTATIONS ============

float noise(float x, float z) {
    int xi = (int)floor(x);
    int zi = (int)floor(z);
    float xf = x - xi;
    float zf = z - zi;
    
    auto hash = [](int a, int b) {
        int h = a * 374761393 + b * 668265263;
        h = (h ^ (h >> 13)) * 1274126177;
        return (h & 0x7fffffff) / (float)0x7fffffff;
    };
    
    float a = hash(xi, zi);
    float b = hash(xi + 1, zi);
    float c = hash(xi, zi + 1);
    float d = hash(xi + 1, zi + 1);
    
    auto smoothstep = [](float t) { return t * t * (3.0f - 2.0f * t); };
    float u = smoothstep(xf);
    float v = smoothstep(zf);
    
    return a * (1-u) * (1-v) + b * u * (1-v) + c * (1-u) * v + d * u * v;
}

float getTerrainHeight(float x, float z) {
    float height = 0;
    float scale = 0.02f;
    height += noise(x * scale, z * scale) * 5.0f;
    height += noise(x * scale * 2, z * scale * 2) * 2.0f;
    height += noise(x * scale * 4, z * scale * 4) * 0.5f;
    return height;
}

TerrainInfo getTerrainInfo(float x, float z) {
    float height = getTerrainHeight(x, z);

    TerrainType type;
    if (height < 0.5f) {
        type = TERRAIN_ROAD;
    } else if (height < 3.0f) {
        type = TERRAIN_GRASS;
    } else {
        type = TERRAIN_DIRT;
    }

    for (const auto& p : puddles) {
        if (glm::length(glm::vec2(x, z) - p.pos) < p.radius) {
            type = TERRAIN_PUDDLE;
            break;
        }
    }

    return {height, type};
}

void spawnBuildings() {
    buildings.clear();
    std::uniform_real_distribution<> x(-50, 50);
    std::uniform_real_distribution<> z(-50, 50);
    for (int i = 0; i < 10; i++) {
        Building b;
        b.position = glm::vec3(x(gen), 0, z(gen));
        b.width = 8.0f;
        b.depth = 8.0f;
        b.height = 12.0f;
        b.position.y = getTerrainInfo(b.position.x, b.position.z).height;
        buildings.push_back(b);
    }
}

void spawnPuddles() {
    puddles.clear();
    std::uniform_real_distribution<> dist(-100, 100);
    std::uniform_real_distribution<> rad(3, 8);
    for (int i = 0; i < 20; i++) {
        Puddle p;
        p.pos = glm::vec2(dist(gen), dist(gen));
        p.radius = rad(gen);
        puddles.push_back(p);
    }
}

void spawnPoliceCar() {
    PoliceCar cop;
    std::uniform_real_distribution<> angle(0, 2 * M_PI);
    float a = angle(gen);
    float spawnDist = 60.0f;
    cop.position = car.position + glm::vec3(cos(a) * spawnDist, 0, sin(a) * spawnDist);
    cop.position.y = getTerrainHeight(cop.position.x, cop.position.z) + 0.5f;
    cop.rotation = 0;
    cop.speed = 0;
    policeCars.push_back(cop);
}

// ============ CHUNK SYSTEM ============

const int CHUNK_SIZE = 32;
const float TILE_SIZE = 2.0f;
const int RENDER_DISTANCE = 5;

struct Chunk {
    int x, z;
    unsigned int VAO, VBO, EBO;
    int indexCount;
};

std::map<std::pair<int, int>, Chunk> chunks;

// ============ SHADERS ============

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

out vec3 Color;
out float Height;
out vec3 FragPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;
    FragPos = worldPos.xyz;
    Color = aColor;
    Height = aPos.y;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec3 Color;
in float Height;
in vec3 FragPos;
out vec4 FragColor;

uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogDensity;

void main() {
    vec3 color = Color;
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(vec3(0, 1, 0), lightDir), 0.0);
    color *= (0.4 + 0.6 * diff);
    
    float factor = 1.0 - (Height / 30.0) * 0.2;
    color *= factor;
    
    float dist = length(cameraPos - FragPos);
    float fogFactor = 1.0 - exp(-fogDensity * dist);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    color = mix(color, fogColor, fogFactor);
    
    FragColor = vec4(color, 1.0);
}
)";

const char* carVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
out vec3 Color;
out vec3 FragPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 carColor;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;
    FragPos = worldPos.xyz;
    Color = carColor;
}
)";

const char* carFragmentShader = R"(
#version 330 core
in vec3 Color;
in vec3 FragPos;
out vec4 FragColor;
uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogDensity;

void main() {
    vec3 color = Color;
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(vec3(0, 1, 0), lightDir), 0.0);
    color *= (0.5 + 0.5 * diff);
    
    float dist = length(cameraPos - FragPos);
    float fogFactor = 1.0 - exp(-fogDensity * dist);
    color = mix(color, fogColor, fogFactor);
    
    FragColor = vec4(color, 1.0);
}
)";

// ============ RENDERING FUNCTIONS ============

void processInput(GLFWwindow* window);
void updateChunks();
unsigned int createShaderProgram(const char* vs, const char* fs);

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    
    if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS && !gameStarted) {
        gameStarted = true;
        survivalTime = 0.0f;
        policeCars.clear();
        bullets.clear();
        spawnPuddles();
        spawnBuildings();
    }
    
    if (!gameStarted) return;
    
    bool w = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS;
    bool s = glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS;
    bool a = glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS;
    bool d = glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS;
    bool space = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
    
    car.update(deltaTime, w, s, a, d, space);
    
    if (w || s) targetVolume = 0.8f;
    else targetVolume = 0.2f;
}

unsigned int createShaderProgram(const char* vs, const char* fs) {
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vs, NULL);
    glCompileShader(vertexShader);

    GLint success;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "Vertex shader compilation failed:\n" << infoLog << std::endl;
    }

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
            
            if (height < 0.5f) {
                vertices.push_back(0.3f); vertices.push_back(0.3f); vertices.push_back(0.3f);
            } else if (height < 3.0f) {
                vertices.push_back(0.35f); vertices.push_back(0.55f); vertices.push_back(0.25f);
            } else {
                vertices.push_back(0.45f); vertices.push_back(0.5f); vertices.push_back(0.45f);
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
    for (auto& key : toRemove) chunks.erase(key);
}

unsigned int createCarVAO() {
    float carVerts[] = {
        -1, 0, -2,  1, 0, -2,  1, 1, -2,  -1, 1, -2,
        -1, 0, 2,   1, 0, 2,   1, 1, 2,   -1, 1, 2,
        -1, 0, -2,  -1, 0, 2,  -1, 1, 2,  -1, 1, -2,
        1, 0, -2,   1, 0, 2,   1, 1, 2,   1, 1, -2,
        -1, 1, -2,  1, 1, -2,  1, 1, 2,   -1, 1, 2,
        -1, 0, -2,  1, 0, -2,  1, 0, 2,   -1, 0, 2
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

    GLFWwindow* window = glfwCreateWindow(1280, 720, "GTA7 - Police Chase", NULL, NULL);
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

    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
        std::cout << "Failed to initialize audio\n";
    } else {
        if (ma_sound_init_from_file(&engine, "enginesound.mp3", MA_SOUND_FLAG_LOOPING, NULL, NULL, &engineSound) != MA_SUCCESS) {
            std::cout << "Failed to load enginesound.mp3\n";
        } else {
            ma_sound_set_volume(&engineSound, 0.0f);
            ma_sound_start(&engineSound);
            isEngineLoaded = true;
        }
    }

    unsigned int terrainShader = createShaderProgram(vertexShaderSource, fragmentShaderSource);
    unsigned int carShader = createShaderProgram(carVertexShader, carFragmentShader);
    unsigned int carVAO = createCarVAO();
    
    car.position.y = getTerrainHeight(0, 0) + 0.5f;
    
    std::cout << "\n=== GTA7 - POLICE CHASE ===\n";
    std::cout << "Press ENTER to start\n";
    std::cout << "W/S - Accelerate/Brake\n";
    std::cout << "A/D - Steer\n";
    std::cout << "SPACE - Drift (NFS style!)\n";
    std::cout << "Avoid police cars and bullets!\n\n";

    float lastFrame = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        deltaTime = fminf(deltaTime, 0.1f);
        lastFrame = currentFrame;
        processInput(window);
        
        if (isEngineLoaded) {
            currentVolume += (targetVolume - currentVolume) * (1.0f - expf(-2.0f * deltaTime));
            ma_sound_set_volume(&engineSound, currentVolume);
        }
        
        if (gameStarted) {
            survivalTime += deltaTime;
            if (survivalTime > highScore) highScore = survivalTime;
            
            spawnTimer += deltaTime;
            if (spawnTimer > 8.0f && policeCars.size() < 5) {
                spawnPoliceCar();
                spawnTimer = 0;
            }
            
            for (auto& cop : policeCars) {
                cop.update(deltaTime, car.position);
                
                float dist = glm::length(cop.position - car.position);
                if (dist < 3.0f) {
                    survivalTime -= 5.0f;
                    if (survivalTime < 0) survivalTime = 0;
                    cop.position = car.position + glm::vec3(50, 0, 50);
                    std::cout << "HIT BY POLICE! -5 seconds\n";
                }
            }
            
            shootTimer += deltaTime;
            if (shootTimer > 2.0f && !policeCars.empty()) {
                auto& cop = policeCars[0];
                glm::vec3 dir = glm::normalize(car.position - cop.position);
                Bullet b;
                b.pos = cop.position + glm::vec3(0, 1, 0);
                b.vel = dir * 30.0f;
                b.lifetime = 3.0f;
                bullets.push_back(b);
                shootTimer = 0;
            }
            
            for (auto& b : bullets) {
                b.pos += b.vel * deltaTime;
                b.lifetime -= deltaTime;
                
                float dist = glm::length(b.pos - car.position);
                if (dist < 2.0f) {
                    survivalTime -= 1.0f;
                    if (survivalTime < 0) survivalTime = 0;
                    b.lifetime = 0;
                    std::cout << "SHOT! -1 second\n";
                }
            }
            bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                [](const Bullet& b) { return b.lifetime <= 0; }), bullets.end());
        }
        
        updateChunks();

        float camDist = 15.0f;
        float camHeight = 6.0f;
        cameraPos.x = car.position.x - sin(car.rotation) * camDist;
        cameraPos.y = car.position.y + camHeight;
        cameraPos.z = car.position.z - cos(car.rotation) * camDist;
        glm::vec3 lookAt = car.position + glm::vec3(0, 1, 0);

        glClearColor(fogColor.r, fogColor.g, fogColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float aspect = (float)framebufferWidth / (float)framebufferHeight;
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
        glm::mat4 view = glm::lookAt(cameraPos, lookAt, glm::vec3(0, 1, 0));

        // Draw terrain
        glUseProgram(terrainShader);
        glUniformMatrix4fv(glGetUniformLocation(terrainShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(terrainShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniform3fv(glGetUniformLocation(terrainShader, "cameraPos"), 1, glm::value_ptr(cameraPos));
        glUniform3fv(glGetUniformLocation(terrainShader, "fogColor"), 1, glm::value_ptr(fogColor));
        glUniform1f(glGetUniformLocation(terrainShader, "fogDensity"), fogDensity);
        glm::mat4 model = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(terrainShader, "model"), 1, GL_FALSE, glm::value_ptr(model));

        for (auto& pair : chunks) {
            glBindVertexArray(pair.second.VAO);
            glDrawElements(GL_TRIANGLES, pair.second.indexCount, GL_UNSIGNED_INT, 0);
        }

        // --- Render Puddles (in main render loop, NOT in Car::update) ---
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(carShader); // reuse car shader for simplicity

        for (const auto& p : puddles) {
            float y = getTerrainHeight(p.pos.x, p.pos.y) + 0.01f;
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, glm::vec3(p.pos.x, y, p.pos.y));
            model = glm::scale(model, glm::vec3(p.radius, 0.01f, p.radius)); // flatten

            glUniformMatrix4fv(glGetUniformLocation(carShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(carShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(carShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3f(glGetUniformLocation(carShader, "carColor"), 0.3f, 0.5f, 1.0f); // blue
            glUniform1f(glGetUniformLocation(carShader, "fogDensity"), 0.0f);
            glUniform3fv(glGetUniformLocation(carShader, "cameraPos"), 1, glm::value_ptr(cameraPos));
            glUniform3fv(glGetUniformLocation(carShader, "fogColor"), 1, glm::value_ptr(fogColor));

            glBindVertexArray(carVAO); // or create a flat quad VAO later
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        glDisable(GL_BLEND);


        



        // Draw player car
        glUseProgram(carShader);
        glUniform3fv(glGetUniformLocation(carShader, "cameraPos"), 1, glm::value_ptr(cameraPos));
        glUniform3fv(glGetUniformLocation(carShader, "fogColor"), 1, glm::value_ptr(fogColor));
        glUniform1f(glGetUniformLocation(carShader, "fogDensity"), fogDensity);
        
        model = glm::mat4(1.0f);
        model = glm::translate(model, car.position);
        model = glm::rotate(model, car.rotation, glm::vec3(0, 1, 0));
        model = glm::rotate(model, car.driftAngle, glm::vec3(0, 1, 0));
        glUniformMatrix4fv(glGetUniformLocation(carShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(carShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(carShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(glGetUniformLocation(carShader, "carColor"), 0.9f, 0.1f, 0.1f);
        
        glBindVertexArray(carVAO);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

        // Draw police cars
        for (auto& cop : policeCars) {
            model = glm::mat4(1.0f);
            model = glm::translate(model, cop.position);
            model = glm::rotate(model, cop.rotation, glm::vec3(0, 1, 0));
            glUniformMatrix4fv(glGetUniformLocation(carShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform3f(glGetUniformLocation(carShader, "carColor"), 0.1f, 0.1f, 0.9f);
            glBindVertexArray(carVAO);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        // Draw buildings
        glUseProgram(carShader);
        glUniform3f(glGetUniformLocation(carShader, "carColor"), 0.4f, 0.4f, 0.4f); // gray
        for (const auto& b : buildings) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, b.position);
            model = glm::scale(model, glm::vec3(b.width, b.height, b.depth));
            glUniformMatrix4fv(glGetUniformLocation(carShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(carShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(carShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3fv(glGetUniformLocation(carShader, "cameraPos"), 1, glm::value_ptr(cameraPos));
            glUniform3fv(glGetUniformLocation(carShader, "fogColor"), 1, glm::value_ptr(fogColor));
            glUniform1f(glGetUniformLocation(carShader, "fogDensity"), fogDensity);
            glBindVertexArray(carVAO); // reuse car VAO (cube)
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }
        
        // Draw bullets (as small red cubes)
        for (auto& b : bullets) {
            model = glm::mat4(1.0f);
            model = glm::translate(model, b.pos);
            model = glm::scale(model, glm::vec3(0.2f));
            glUniformMatrix4fv(glGetUniformLocation(carShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniform3f(glGetUniformLocation(carShader, "carColor"), 1.0f, 0.0f, 0.0f);
            glBindVertexArray(carVAO);
            glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        }

        // Print HUD to console (in a real game you'd render to screen)
        if (gameStarted) {
            static float printTimer = 0;
            printTimer += deltaTime;
            if (printTimer > 1.0f) {
                std::cout << "Time: " << (int)survivalTime << "s | High Score: " << (int)highScore 
                         << "s | Police: " << policeCars.size() << " | Speed: " << (int)car.speed 
                         << (car.isDrifting ? " [DRIFT]" : "") << "\n";
                printTimer = 0;
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (isEngineLoaded) ma_sound_uninit(&engineSound);
    ma_engine_uninit(&engine);
    glfwTerminate();
    return 0;
}
