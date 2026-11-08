#pragma once

#include <raylib.h>

namespace slopengine {

inline bool gpuReady() {
    return IsWindowReady();
}

inline void uploadMeshIfGpu(Mesh* mesh, bool dynamic) {
    if (gpuReady()) {
        UploadMesh(mesh, dynamic);
    }
}

inline Texture2D loadTextureFromImageIfGpu(Image image) {
    if (gpuReady()) {
        return LoadTextureFromImage(image);
    }
    return Texture2D{0, image.width, image.height, image.mipmaps, image.format};
}

inline Texture2D loadTextureIfGpu(const char* path) {
    if (gpuReady()) {
        return LoadTexture(path);
    }
    Image image = LoadImage(path);
    const Texture2D texture = loadTextureFromImageIfGpu(image);
    UnloadImage(image);
    return texture;
}

inline Shader loadShaderFromMemoryIfGpu(const char* vertex, const char* fragment) {
    if (gpuReady()) {
        return LoadShaderFromMemory(vertex, fragment);
    }
    return Shader{};
}

}
