#pragma once

#include <filesystem>

#if defined(_WIN32)
#include <d3d11.h>
#else
#include <SDL3/SDL_gpu.h>
#endif

// Cover artwork loaded into the launcher's graphics backend. Windows keeps the
// Direct3D 11 view; the other platforms use SDL_GPU, whose texture handle is
// also what the ImGui SDL_GPU backend expects as an ImTextureID.
struct LoadedTexture {
#if defined(_WIN32)
  ID3D11ShaderResourceView* srv = nullptr;
#else
  SDL_GPUTexture* srv = nullptr;
#endif
  int width = 0;
  int height = 0;

  void Release() {
#if defined(_WIN32)
    if (srv) {
      srv->Release();
      srv = nullptr;
    }
#else
    // SDL_GPU textures are released by the caller, which owns the device.
#endif
    width = 0;
    height = 0;
  }
};

#if defined(_WIN32)
LoadedTexture LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path);
#else
LoadedTexture LoadTextureFromFile(SDL_GPUDevice* device, const std::filesystem::path& path);
void ReleaseTexture(SDL_GPUDevice* device, LoadedTexture& texture);
#endif