#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// The game executable next to the launcher carries the native renderer next to
// the Xenos plugin (NARUTORISE_NATIVE_RENDERER_BOOTSTRAP builds).
#ifdef NARUTORISE_LAUNCHER_NATIVE_RENDERER
inline constexpr bool kNativeRendererBuilt = true;
#else
inline constexpr bool kNativeRendererBuilt = false;
#endif

struct GameConfig {
  std::string gpu_plugin = "xenos";
  // "xenos" (the emulated GPU) or "native" (Direct3D 12); saved only when the
  // executable carries the native renderer.
  std::string renderer = "xenos";
  // The graphics card by its name, for both renderers; empty = automatic.
  std::string graphics_device = "";
  bool d3d12_readback_resolve = true;
  bool skip_intro_videos = true;

  // Game language: 1 = English, 4 = French, 3 = German, 5 = Spanish, 6 = Italian.
  std::uint32_t user_language = 1;

  bool log_verbose = false;
  std::string log_level = "info";

  bool protect_zero = false;

  std::string input_backend = "sdl";
  bool mnk_mode = false;

  bool fullscreen = true;
  bool vsync = true;
  bool d3d12_host_vsync = true;
  double video_mode_refresh_rate = 60.0;
  int resolution_scale = 2;
  int draw_resolution_scale_x = 2;
  int draw_resolution_scale_y = 2;
  bool d3d12_allow_variable_refresh_rate_and_tearing = true;

  int anisotropic_override = 5;

  // Native renderer only, saved with the renderer choice.
  int msaa_samples = 0;  // 0 = the game's 4x, or 2, 4, 8
  bool transparency_antialiasing = false;
  bool unlock_fps = false;
  double brightness = 0.5;  // 0.5 = the game's
  double internal_resolution_scale = 0.0;  // fractional scale; 0 = resolution_scale
  int max_frame_latency = 1;
  bool triple_buffering = false;

  std::string swap_post_effect = "fxaa";
  std::string present_effect = "cas";
  bool present_dither = false;
  bool show_fps_overlay = false;

  // 0.0 = 16:9 native, 2.3889 = 21:9, 3.5556 = 32:9
  double ultrawide_target_aspect = 2.3889;

  std::string custom_game_root = "";
  // Saves and user content; empty keeps Documents/narutorise. Read only: the
  // launcher passes it to the game on the command line (main.cpp).
  std::string user_data_root = "";

  // Launcher interface: en (default), pt_BR, fr, de, es, it, ru.
  std::string launcher_language = "en";
};

class ConfigManager {
 public:
  static bool Load(const std::filesystem::path& toml_path, GameConfig& config);
  static bool Save(const std::filesystem::path& toml_path, const GameConfig& config);
};
