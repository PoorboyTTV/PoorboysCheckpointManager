#pragma once
// HCM keeps the settings (HCMInternalConfig.xml) and the hotkeys (HCMHotkeyConfig.xml) in its own folder, so a new
// version unzipped to a new folder started from the defaults. PCM also keeps a copy of each file in
// %LOCALAPPDATA%\PoorboysCheckpointManager\ every time it saves one; a folder without its own file starts from that
// copy. A folder that has its own file keeps using it, as before.

namespace PoorboySharedConfig
{
	// where the copy of fileName is kept; empty when %LOCALAPPDATA% can't be read
	inline std::filesystem::path copyPath(std::string_view fileName)
	{
		wchar_t local[MAX_PATH];
		const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) return {};
		return std::filesystem::path(local) / L"PoorboysCheckpointManager" / std::filesystem::path(fileName);
	}

	// the copy, when there is one to start from
	inline std::optional<std::filesystem::path> copyToLoad(std::string_view fileName)
	{
		const auto copy = copyPath(fileName);
		std::error_code ec;
		if (copy.empty() || !std::filesystem::is_regular_file(copy, ec)) return std::nullopt;
		return copy;
	}

	// after savedFile (in the PCM folder) was written: the copy follows it
	inline void keepCopy(const std::string& savedFile, std::string_view fileName)
	{
		const auto copy = copyPath(fileName);
		if (copy.empty()) return;
		std::error_code ec;
		std::filesystem::create_directories(copy.parent_path(), ec);
		std::filesystem::copy_file(std::filesystem::path(savedFile), copy, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) PLOG_ERROR << "Could not keep a copy of " << savedFile << " in " << copy.wstring() << ": " << ec.message();
		else PLOG_DEBUG << "Kept a copy of " << savedFile << " at " << copy.wstring();
	}
}
