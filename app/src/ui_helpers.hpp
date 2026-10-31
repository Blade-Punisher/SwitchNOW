#pragma once

#include <string>

#include "gfn_client.hpp"

namespace opennow
{

void ShowDialog(const std::string& title, const std::string& body);
void ShowModalDialog(const std::string& title, const std::string& body);
void ShowError(const std::string& title, const std::string& body);
void ResumeSavedSessionDialog(const GfnClient& client, const AuthSession& auth);
void SaveDiagnosticReportDialog();
void LaunchSessionDialog(const GfnClient& client, const AuthSession& auth,
                         const std::string& launch_app_id, const std::string& title,
                         const std::string& launch_store = "",
                         const std::string& internal_title = "",
                         const std::string& history_game_id = "",
                         const std::string& image_url = "");

} // namespace opennow
