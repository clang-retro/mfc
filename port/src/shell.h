// shell.h - the shell namespace's pieces shared by its files: item ID
// lists as path components (see shell_namespace.cpp) and paths.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <windows.h>
#include <shlobj.h>

#include <string>
#include <vector>

namespace winport {

// An ID list's items, each a path component, and back (task memory).
std::vector<std::string> PidlItems(LPCITEMIDLIST pidl);
LPITEMIDLIST MakePidl(const std::vector<std::string>& items);
// Components to "/a/b" ("/" for none), and a path to its components,
// with "." and ".." resolved.
std::string JoinPath(const std::vector<std::string>& items);
std::vector<std::string> SplitPath(const std::string& path);
// A Windows or POSIX path, from the current directory when relative.
std::string AbsolutePath(const std::wstring& name);
bool IsDir(const std::string& path);
// A folder's entries, sorted by name, ignoring case.
std::vector<std::string> ListDir(const std::string& path, bool folders, bool files, bool hidden);
// A shortcut (.lnk) file's target, with its arguments and working
// directory; empty when the file is not a shortcut (shell_link.cpp).
std::wstring ShortcutTarget(const std::wstring& file, std::wstring* args, std::wstring* dir);
namespace ole {
HRESULT GetShellLinkClassObject(REFIID riid, void** out);
}

}  // namespace winport
