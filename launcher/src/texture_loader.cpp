#include "texture_loader.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_IMPLEMENTATION
#if defined(_WIN32)
#define STBI_WINDOWS_UTF8
#endif
#include <stb_image.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(_WIN32)
LoadedTexture LoadTextureFromFile(ID3D11Device* device, const std::filesystem::path& path) {
  LoadedTexture tex;
  if (!device || !std::filesystem::exists(path)) {
    return tex;
  }

  int channels = 0;
  const auto utf8 = path.u8string();
  unsigned char* data = stbi_load(reinterpret_cast<const char*>(utf8.c_str()), &tex.width, &tex.height, &channels, 4);
  if (!data) {
    return tex;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(tex.width);
  desc.Height = static_cast<UINT>(tex.height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA init_data = {};
  init_data.pSysMem = data;
  init_data.SysMemPitch = static_cast<UINT>(tex.width * 4);

  ID3D11Texture2D* pTexture = nullptr;
  HRESULT hr = device->CreateTexture2D(&desc, &init_data, &pTexture);
  if (SUCCEEDED(hr) && pTexture) {
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(pTexture, &srv_desc, &tex.srv);
    pTexture->Release();
  }

  stbi_image_free(data);
  return tex;
}
#else
LoadedTexture LoadTextureFromFile(SDL_GPUDevice* device, const std::filesystem::path& path) {
  LoadedTexture tex;
  if (!device || !std::filesystem::exists(path)) {
    return tex;
  }

  int channels = 0;
  const auto utf8 = path.u8string();
  unsigned char* data = stbi_load(reinterpret_cast<const char*>(utf8.c_str()), &tex.width, &tex.height, &channels, 4);
  if (!data) {
    return tex;
  }

  SDL_GPUTextureCreateInfo texture_info = {};
  texture_info.type = SDL_GPU_TEXTURETYPE_2D;
  texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  texture_info.width = static_cast<Uint32>(tex.width);
  texture_info.height = static_cast<Uint32>(tex.height);
  texture_info.layer_count_or_depth = 1;
  texture_info.num_levels = 1;
  texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
  tex.srv = SDL_CreateGPUTexture(device, &texture_info);
  if (!tex.srv) {
    stbi_image_free(data);
    tex.width = tex.height = 0;
    return tex;
  }

  const Uint32 size = static_cast<Uint32>(tex.width) * static_cast<Uint32>(tex.height) * 4;
  SDL_GPUTransferBufferCreateInfo transfer_info = {};
  transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transfer_info.size = size;
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
  if (!transfer) {
    SDL_ReleaseGPUTexture(device, tex.srv);
    tex.srv = nullptr;
    tex.width = tex.height = 0;
    stbi_image_free(data);
    return tex;
  }

  void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
  if (!mapped) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, tex.srv);
    tex.srv = nullptr;
    tex.width = tex.height = 0;
    stbi_image_free(data);
    return tex;
  }
  SDL_memcpy(mapped, data, size);
  SDL_UnmapGPUTransferBuffer(device, transfer);
  stbi_image_free(data);

  SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
  if (!command) {
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, tex.srv);
    tex.srv = nullptr;
    tex.width = tex.height = 0;
    return tex;
  }
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
  SDL_GPUTextureTransferInfo source = {};
  source.transfer_buffer = transfer;
  source.pixels_per_row = static_cast<Uint32>(tex.width);
  source.rows_per_layer = static_cast<Uint32>(tex.height);
  SDL_GPUTextureRegion destination = {};
  destination.texture = tex.srv;
  destination.w = static_cast<Uint32>(tex.width);
  destination.h = static_cast<Uint32>(tex.height);
  destination.d = 1;
  SDL_UploadToGPUTexture(copy, &source, &destination, false);
  SDL_EndGPUCopyPass(copy);
  SDL_SubmitGPUCommandBuffer(command);
  SDL_WaitForGPUIdle(device);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return tex;
}

void ReleaseTexture(SDL_GPUDevice* device, LoadedTexture& texture) {
  if (device && texture.srv) {
    SDL_ReleaseGPUTexture(device, texture.srv);
  }
  texture.srv = nullptr;
  texture.width = texture.height = 0;
}
#endif