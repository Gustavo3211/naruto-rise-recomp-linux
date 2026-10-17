#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "content_manager.h"
#include "folders.h"
#include "localization.h"
#include "pages/pages.h"
#include "widgets.h"

namespace launcher_ui {
namespace {

// The folder dialog answers on another thread.
std::mutex g_dialog_mutex;
std::string g_selected_folder;
bool g_dialog_pending = false;
bool g_dialog_updated = false;

void SDLCALL DlcFolderDialogCallback(void*, const char* const* files, int) {
  std::lock_guard lock(g_dialog_mutex);
  if (files && files[0]) {
    g_selected_folder = files[0];
    g_dialog_updated = true;
  }
  g_dialog_pending = false;
}

bool DialogPending() {
  std::lock_guard lock(g_dialog_mutex);
  return g_dialog_pending;
}

// Listing reads every content header, so the list is read when the page
// opens and after an installation, not in every frame.
std::vector<launcher_content::ContentInfo> g_installed;
bool g_installed_stale = true;
int g_last_frame = -1;

const std::vector<launcher_content::ContentInfo>& InstalledDlc() {
  const int frame = ImGui::GetFrameCount();
  if (g_installed_stale || frame != g_last_frame + 1) {
    g_installed = launcher_content::ListInstalledDlc();
    g_installed_stale = false;
  }
  g_last_frame = frame;
  return g_installed;
}

}  // namespace

void ApplyDlcFolderSelection(PageContext& context) {
  std::lock_guard lock(g_dialog_mutex);
  if (!g_dialog_updated) return;
  g_dialog_updated = false;
  const auto selected = std::filesystem::u8path(g_selected_folder);
  std::error_code error_code;
  if (!std::filesystem::is_directory(selected, error_code)) return;
  // STFS packages are staged into the game's dlc/ folder and installed by the
  // game on the next launch (real content headers with the license from the
  // package); loose content is copied directly into the user data tree.
  std::string error;
  const int staged = launcher_content::InstallDlc(selected, context.base_dir, error);
  g_installed_stale = true;
  if (staged > 0) {
    context.status_message = Tr(TextId::DlcStfsStaged);
  } else if (staged == 0) {
    context.status_message = Tr(TextId::DlcInstallSuccess);
  } else {
    context.status_message = std::string(Tr(TextId::DlcInstallFailed)) + " " + error;
  }
}

void RenderDlcPage(PageContext& context) {
  Heading(Tr(TextId::TabDlc));
  Muted(Tr(TextId::DlcIntro));
  ImGui::Dummy(ImVec2(0, 16));

  BeginCard("InstallDlc", 0);
  Heading(Tr(TextId::DlcInstallLoose), 21);
  Muted(Tr(TextId::DlcInstallLooseHint));
  ImGui::Spacing();
  ImGui::BeginDisabled(DialogPending());
  if (PrimaryButton(Tr(TextId::DlcSelectLooseFolder), ImVec2(-1, 38))) {
    { std::lock_guard lock(g_dialog_mutex); g_dialog_pending = true; }
    SDL_ShowOpenFolderDialog(DlcFolderDialogCallback, nullptr, context.window, nullptr, false);
  }
  ImGui::EndDisabled();
  EndCard();
  ImGui::Dummy(ImVec2(0, 14));

  BeginCard("InstalledDlc", 0);
  Heading(Tr(TextId::DlcInstalledHeading), 21);
  const auto& installed = InstalledDlc();
  if (installed.empty()) {
    Muted(Tr(TextId::DlcNoInstalled));
  } else {
    // Up to five names without scrolling; the folder is in each name's tooltip.
    constexpr float kListPadding = 8;
    const float rows = static_cast<float>(std::min<size_t>(installed.size(), 5));
    const float height = rows * ImGui::GetTextLineHeightWithSpacing() + 2 * kListPadding;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, kListPadding));
    ImGui::BeginChild("DlcList", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    for (const auto& dlc : installed) {
      ImGui::BulletText("%s", dlc.name.c_str());
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", PathText(dlc.path.filename()).c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
  }
  ImGui::Spacing();
  const float half_width = (ImGui::GetContentRegionAvail().x - 10) / 2;
  if (ImGui::Button(Tr(TextId::DlcPrepareFolders), ImVec2(half_width, 36))) {
    std::string error;
    context.status_message = launcher_content::EnsureDlcFolders(error) ? Tr(TextId::DlcFoldersReady) : error;
  }
  ImGui::SameLine();
  if (ImGui::Button(Tr(TextId::DlcOpenContentFolder), ImVec2(half_width, 36))) {
    OpenFolder(launcher_content::GetDlcRoot());
  }
  EndCard();
}

}  // namespace launcher_ui
