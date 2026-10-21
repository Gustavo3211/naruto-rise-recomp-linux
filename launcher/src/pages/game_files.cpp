#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_process.h>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "folders.h"
#include "localization.h"
#include "pages/pages.h"
#include "widgets.h"

namespace launcher_ui {

// The folder dialog answers on another thread.
std::mutex g_dialog_mutex;
std::string g_selected_folder;
bool g_dialog_pending = false;
bool g_dialog_updated = false;

// ISO extraction state.
std::mutex g_iso_mutex;
std::string g_selected_iso;
bool g_iso_pending = false;
bool g_iso_extracting = false;
std::string g_iso_status;
bool g_iso_finished = false;
bool g_iso_success = false;

void SDLCALL FolderDialogCallback(void*, const char* const* files, int) {
  std::lock_guard lock(g_dialog_mutex);
  if (files && files[0]) {
    g_selected_folder = files[0];
    g_dialog_updated = true;
  }
  g_dialog_pending = false;
}

void SDLCALL ISODialogCallback(void*, const char* const* files, int) {
  std::lock_guard lock(g_iso_mutex);
  if (files && files[0]) {
    g_selected_iso = files[0];
    g_iso_pending = true;
    g_iso_extracting = true;
    g_iso_status = Tr(TextId::ISOExtractInProgress);
    g_iso_finished = false;
  }
  g_dialog_pending = false;
}

bool DialogPending() {
  std::lock_guard lock(g_dialog_mutex);
  return g_dialog_pending;
}

// Find extract-xiso binary
std::filesystem::path FindExtractXiso() {
  // 1. Check tools/extract-xiso/extract-xiso (Linux native)
  std::filesystem::path local = "tools/extract-xiso/extract-xiso";
  if (std::filesystem::exists(local)) {
    auto perms = std::filesystem::status(local).permissions();
    if ((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
      return std::filesystem::absolute(local);
    }
  }

  // 2. Check tools/extract-xiso/extract-xiso.exe (Windows via Wine)
  std::filesystem::path local_exe = "tools/extract-xiso/extract-xiso.exe";
  if (std::filesystem::exists(local_exe)) {
    if (std::filesystem::exists("/usr/bin/wine")) {
      return std::filesystem::absolute(local_exe);
    }
  }

  // 3. Check system PATH
  if (std::filesystem::exists("/usr/bin/extract-xiso")) {
    return "/usr/bin/extract-xiso";
  }

  return {};
}

// Extract ISO in background thread
void ExtractISOThread(const std::filesystem::path& iso_path, const std::filesystem::path& dest_dir) {
  std::filesystem::path extract_xiso = FindExtractXiso();
  if (extract_xiso.empty()) {
    {
      std::lock_guard lock(g_iso_mutex);
      g_iso_status = std::string(Tr(TextId::ISOExtractFailed)) + "extract-xiso not found";
      g_iso_extracting = false;
      g_iso_finished = true;
      g_iso_success = false;
    }
    return;
  }

  // Create destination directory
  std::error_code ec;
  std::filesystem::create_directories(dest_dir, ec);

  // Run extract-xiso
  std::vector<std::string> cmd_args;
  if (extract_xiso.extension() == ".exe") {
    cmd_args = {"/usr/bin/wine", extract_xiso.string(), "-x", "-d", dest_dir.string(), iso_path.string()};
  } else {
    cmd_args = {extract_xiso.string(), "-x", "-d", dest_dir.string(), iso_path.string()};
  }

  std::vector<const char*> args;
  for (const auto& arg : cmd_args) {
    args.push_back(arg.c_str());
  }
  args.push_back(nullptr);

  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_INHERITED);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_INHERITED);
  SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, ".");

  SDL_Process* proc = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);

  if (!proc) {
    {
      std::lock_guard lock(g_iso_mutex);
      g_iso_status = std::string(Tr(TextId::ISOExtractFailed)) + SDL_GetError();
      g_iso_extracting = false;
      g_iso_finished = true;
      g_iso_success = false;
    }
    return;
  }

  // Wait for process to finish
  int exit_code = 0;
  bool waited = SDL_WaitProcess(proc, true, &exit_code);
  SDL_DestroyProcess(proc);

  {
    std::lock_guard lock(g_iso_mutex);
    g_iso_extracting = false;
    g_iso_finished = true;
    if (waited && exit_code == 0) {
      g_iso_status = std::string(Tr(TextId::ISOExtractSuccess)) + dest_dir.string();
      g_iso_success = true;
    } else {
      g_iso_status = std::string(Tr(TextId::ISOExtractFailed)) + (waited ? "exit code " + std::to_string(exit_code) : "process failed");
      g_iso_success = false;
    }
  }
}

void ApplyGameFolderSelection(PageContext& context) {
  std::lock_guard lock(g_dialog_mutex);
  if (!g_dialog_updated) return;
  g_dialog_updated = false;
  std::error_code error;
  if (std::filesystem::is_regular_file(std::filesystem::u8path(g_selected_folder) / "default.xex", error)) {
    context.config.custom_game_root = g_selected_folder;
    context.status_message = Tr(TextId::PathUpdatedMsg) + g_selected_folder;
  } else {
    context.status_message = Tr(TextId::FolderHint);
  }
}

void RenderGameFilesPage(PageContext& context) {
  const GameStatus& game = context.game;
  Heading(Tr(TextId::TabGameFiles));
  Muted(Tr(TextId::FilesIntro));
  ImGui::Dummy(ImVec2(0, 16));

  BeginCard("Folder", 0);
  Heading(Tr(TextId::FolderHeading), 21);
  ImGui::TextColored(game.FilesFound() ? kGreen : kWarning, "%s",
                     Tr(game.FilesFound() ? TextId::FilesDetected : TextId::StatusNotFound));
  ImGui::Spacing();
  // Scrollable path: long locations cannot overlap the actions.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
  ImGui::BeginChild("FolderPath", ImVec2(0, 48), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  const std::string path = game.FilesFound() ? PathText(game.game_root)
                           : context.config.custom_game_root.empty() ? Tr(TextId::NoneDetected)
                                                                     : context.config.custom_game_root;
  ImGui::TextUnformatted(path.c_str());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
  ImGui::EndChild();
  ImGui::PopStyleVar();
  Muted(Tr(TextId::FolderHint));
  ImGui::Spacing();
  ImGui::BeginDisabled(DialogPending());
  if (PrimaryButton(Tr(TextId::BtnSelectFolder), ImVec2(-1, 38))) {
    { std::lock_guard lock(g_dialog_mutex); g_dialog_pending = true; }
    SDL_ShowOpenFolderDialog(FolderDialogCallback, nullptr, context.window, nullptr, false);
  }
  ImGui::EndDisabled();
  EndCard();

  // ISO Extraction button
  ImGui::Spacing();
  BeginCard("ISOExtract", 0);
  Heading(Tr(TextId::ISOExtractTitle), 21);
  Muted(Tr(TextId::ISOExtractHint));
  ImGui::Spacing();

  // Check if extract-xiso is available
  bool has_extract_xiso = !FindExtractXiso().empty();

  ImGui::BeginDisabled(DialogPending() || g_iso_extracting || !has_extract_xiso);
  if (PrimaryButton(Tr(TextId::BtnExtractISO), ImVec2(-1, 38))) {
    { std::lock_guard lock(g_dialog_mutex); g_dialog_pending = true; }
    { std::lock_guard lock(g_iso_mutex); g_selected_iso.clear(); g_iso_pending = false; }
    SDL_DialogFileFilter filter = { "Xbox 360 ISO", "iso" };
    SDL_ShowOpenFileDialog(ISODialogCallback, nullptr, context.window, &filter, 1, nullptr, false);
  }
  ImGui::EndDisabled();

  if (!has_extract_xiso) {
    Muted("extract-xiso not found. Install it or run ./packaging/setup.sh");
  }

  // Show ISO extraction progress
  {
    std::lock_guard lock(g_iso_mutex);
    if (g_iso_pending && !g_selected_iso.empty()) {
      g_iso_pending = false;
      // Start extraction in background
      std::filesystem::path dest = std::filesystem::absolute("game_root");
      std::thread(ExtractISOThread, g_selected_iso, dest).detach();
    }
    if (g_iso_extracting) {
      ImGui::Spacing();
      ImGui::TextColored(kAccent, "%s", g_iso_status.c_str());
    } else if (g_iso_finished) {
      ImGui::Spacing();
      if (g_iso_success) {
        ImGui::TextColored(kGreen, "%s", g_iso_status.c_str());
        // Update game status if extraction succeeded
        context.config.custom_game_root = std::filesystem::absolute("game_root").string();
        context.status_message = g_iso_status;
        // Reset extraction state
        g_iso_finished = false;
        g_iso_status.clear();
        g_iso_success = false;
      } else {
        ImGui::TextColored(kWarning, "%s", g_iso_status.c_str());
        g_iso_finished = false;
        g_iso_status.clear();
      }
    }
  }

  if (!has_extract_xiso) {
    Muted("Dica: rode ./packaging/setup.sh para baixar o extract-xiso automaticamente");
  }
  EndCard();

  const float half_width = (ImGui::GetContentRegionAvail().x - 10) / 2;
  if (ImGui::Button(Tr(TextId::BtnResetDefault), ImVec2(half_width, 36))) {
    context.config.custom_game_root.clear();
    context.status_message = Tr(TextId::PathResetMsg);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!game.FilesFound());
  if (ImGui::Button(Tr(TextId::BtnOpenFolder), ImVec2(half_width, 36))) OpenFolder(game.game_root);
  ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0, 14));

  BeginCard("SetupHelp", 0);
  ImGui::TextColored(kAccent, "%s", Tr(TextId::SetupHeading));
  ImGui::Spacing();
  Muted(Tr(TextId::SetupHint));
  EndCard();
}

}  // namespace launcher_ui