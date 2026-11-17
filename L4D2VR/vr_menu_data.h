#pragma once

// What the in-world menu reads from the game folder: Portal 2's own localised texts
// and the player's save files. File access only; no engine calls.

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace VrMenuData {

inline std::wstring Upper(std::wstring text)
{
    for (wchar_t &c : text) c = static_cast<wchar_t>(std::towupper(c));
    return text;
}

inline std::wstring Widen(const std::string &utf8)
{
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

inline std::vector<char> ReadFile(const std::string &path, std::size_t limit)
{
    std::ifstream stream(path, std::ios::binary);
    std::vector<char> bytes;
    if (!stream) return bytes;
    bytes.resize(limit);
    stream.read(bytes.data(), static_cast<std::streamsize>(limit));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
    return bytes;
}

// "token" "text" pairs of a Source localisation file, keyed in lower case.
inline std::map<std::string, std::wstring> ParseTokens(const std::wstring &text)
{
    std::map<std::string, std::wstring> tokens;
    std::size_t at = 0;
    const auto quoted = [&](std::size_t lineEnd, std::wstring &out) {
        const std::size_t open = text.find(L'"', at);
        if (open == std::wstring::npos || open >= lineEnd) return false;
        out.clear();
        std::size_t i = open + 1;
        for (; i < lineEnd && text[i] != L'"'; ++i) {
            if (text[i] == L'\\' && i + 1 < lineEnd) {
                ++i;
                out += text[i] == L'n' ? L' ' : text[i];
            } else {
                out += text[i];
            }
        }
        at = i + 1;
        return i < lineEnd;
    };
    while (at < text.size()) {
        std::size_t lineEnd = text.find(L'\n', at);
        if (lineEnd == std::wstring::npos) lineEnd = text.size();
        std::wstring key, value;
        if (quoted(lineEnd, key) && quoted(lineEnd, value) && !key.empty() && key.size() < 96) {
            std::string narrow;
            for (wchar_t c : key) narrow += static_cast<char>(c < 128 ? std::tolower(static_cast<int>(c)) : '?');
            tokens.emplace(std::move(narrow), std::move(value));
        }
        at = lineEnd + 1;
    }
    return tokens;
}

// Source writes these files as UTF-16 LE with a byte-order mark.
inline std::wstring ReadUtf16(const std::string &path)
{
    const std::vector<char> bytes = ReadFile(path, 8u << 20);
    if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE)
        return {};
    std::wstring text((bytes.size() - 2) / 2, L'\0');
    std::memcpy(text.data(), bytes.data() + 2, text.size() * 2);
    return text;
}

// The language Steam runs Portal 2 in, from its app manifest; English when unknown.
inline std::string GameLanguage(const std::string &manifestText)
{
    const std::size_t key = manifestText.find("\"language\"");
    if (key == std::string::npos) return "english";
    const std::size_t open = manifestText.find('"', key + 10);
    const std::size_t close = open == std::string::npos ? open : manifestText.find('"', open + 1);
    if (close == std::string::npos) return "english";
    std::string language = manifestText.substr(open + 1, close - open - 1);
    const bool plain = !language.empty() && language.size() < 24 &&
        std::all_of(language.begin(), language.end(), [](char c) { return c >= 'a' && c <= 'z'; });
    return plain ? language : "english";
}

class Strings {
public:
    // gameRoot is the Portal 2 folder with a trailing separator, or empty for the working directory.
    void Load(const std::string &gameRoot)
    {
        const std::vector<char> manifest = ReadFile(gameRoot + "..\\..\\appmanifest_620.acf", 1u << 16);
        m_Language = GameLanguage(std::string(manifest.begin(), manifest.end()));
        m_Tokens.clear();
        for (const char *file : {"basemodui_", "portal2_"}) {
            auto tokens = ParseTokens(ReadUtf16(gameRoot + "portal2\\resource\\" + file + m_Language + ".txt"));
            if (tokens.empty() && m_Language != "english")
                tokens = ParseTokens(ReadUtf16(gameRoot + "portal2\\resource\\" + file + "english.txt"));
            m_Tokens.insert(tokens.begin(), tokens.end());
        }
    }

    std::wstring Get(const char *token, const wchar_t *fallback) const
    {
        std::string key = token;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto found = m_Tokens.find(key);
        return Upper(found != m_Tokens.end() && !found->second.empty() ? found->second : std::wstring(fallback));
    }

    const std::string &Language() const { return m_Language; }
    std::size_t Count() const { return m_Tokens.size(); }

private:
    std::string m_Language = "english";
    std::map<std::string, std::wstring> m_Tokens;
};

// Single-player maps in campaign order, by chapter. Unknown maps (community, co-op) are chapter 0.
inline int ChapterOfMap(const std::string &map)
{
    static const std::pair<int, const char *> chapters[] = {
        {1, "sp_a1_intro1 sp_a1_intro2 sp_a1_intro3 sp_a1_intro4 sp_a1_intro5 sp_a1_intro6 sp_a1_intro7 sp_a1_wakeup"},
        {2, "sp_a2_intro sp_a2_laser_intro sp_a2_laser_stairs sp_a2_dual_lasers sp_a2_laser_over_goo sp_a2_catapult_intro "
            "sp_a2_trust_fling sp_a2_pit_flings sp_a2_fizzler_intro"},
        {3, "sp_a2_sphere_peek sp_a2_ricochet sp_a2_bridge_intro sp_a2_bridge_the_gap sp_a2_turret_intro "
            "sp_a2_laser_relays sp_a2_turret_blocker sp_a2_laser_vs_turret sp_a2_pull_the_rug"},
        {4, "sp_a2_column_blocker sp_a2_laser_chaining sp_a2_triple_laser sp_a2_bts1 sp_a2_bts2"},
        {5, "sp_a2_bts3 sp_a2_bts4 sp_a2_bts5 sp_a2_bts6 sp_a2_core"},
        {6, "sp_a3_00 sp_a3_01 sp_a3_03 sp_a3_jump_intro sp_a3_bomb_flings sp_a3_crazy_box sp_a3_transition01"},
        {7, "sp_a3_speed_ramp sp_a3_speed_flings sp_a3_portal_intro sp_a3_end"},
        {8, "sp_a4_intro sp_a4_tb_intro sp_a4_tb_trust_drop sp_a4_tb_wall_button sp_a4_tb_polarity sp_a4_tb_catch "
            "sp_a4_stop_the_box sp_a4_laser_catapult sp_a4_laser_platform sp_a4_speed_tb_catch sp_a4_jump_polarity"},
        {9, "sp_a4_finale1 sp_a4_finale2 sp_a4_finale3 sp_a4_finale4"},
        {10, "sp_a5_credits"}};
    if (map.empty()) return 0;
    const std::string needle = " " + map + " ";
    for (const auto &chapter : chapters)
        if ((" " + std::string(chapter.second) + " ").find(needle) != std::string::npos) return chapter.first;
    return 0;
}

struct Save {
    std::string name;     // file name without ".sav", as the load and save commands take it
    std::string map;
    std::wstring when;    // local date and time, in the user's regional format
    std::wstring played;  // time played, "h:mm"
    bool autosave = false;
    std::uint64_t time = 0;
};

// Only names the console takes as one plain word are ever passed to a command.
inline bool SafeSaveName(const std::string &name)
{
    return !name.empty() && name.size() <= 48 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

// "JSAV", version, header size, token count, token bytes; then the token table and the game
// header: the map name at +12 and an 80-character comment at +144 ending in "minutes:seconds".
inline bool ParseSaveHeader(const std::vector<char> &bytes, std::string &map, std::wstring &played)
{
    if (bytes.size() < 20 || std::memcmp(bytes.data(), "JSAV", 4) != 0) return false;
    std::uint32_t tokenBytes = 0;
    std::memcpy(&tokenBytes, bytes.data() + 16, 4);
    const std::size_t header = 20 + static_cast<std::size_t>(tokenBytes);
    if (tokenBytes > (1u << 20) || bytes.size() < header + 144 + 80) return false;
    map.clear();
    for (std::size_t i = header + 12; i < header + 12 + 32 && bytes[i]; ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (!std::isalnum(c) && c != '_' && c != '-') return false;
        map += static_cast<char>(std::tolower(c));
    }
    std::string comment(bytes.data() + header + 144, 80);
    comment = comment.substr(0, comment.find('\0'));
    played.clear();
    const std::size_t space = comment.find_last_of(' ');
    const std::string clock = space == std::string::npos ? std::string() : comment.substr(space + 1);
    const std::size_t colon = clock.find(':');
    if (colon != std::string::npos && colon > 0 && colon + 1 < clock.size() &&
        std::all_of(clock.begin(), clock.end(), [](unsigned char c) { return std::isdigit(c) || c == ':'; })) {
        const int minutes = std::atoi(clock.substr(0, colon).c_str());
        wchar_t text[24]{};
        swprintf_s(text, L"%d:%02d", minutes / 60, minutes % 60);
        played = text;
    }
    return !map.empty();
}

inline std::wstring LocalDateTime(const FILETIME &utc)
{
    SYSTEMTIME system{}, local{};
    if (!FileTimeToSystemTime(&utc, &system) || !SystemTimeToTzSpecificLocalTime(nullptr, &system, &local)) return {};
    wchar_t date[64]{}, time[64]{};
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 64);
    return std::wstring(date) + L"  " + time;
}

// The saves of the Steam user who played last, newest first.
inline std::vector<Save> ListSaves(const std::string &gameRoot)
{
    const std::string root = gameRoot + "portal2\\SAVE\\";
    std::vector<std::string> folders{root};
    WIN32_FIND_DATAA entry{};
    HANDLE find = FindFirstFileA((root + "*").c_str(), &entry);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::string name = entry.cFileName;
            if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != "." && name != "..")
                folders.push_back(root + name + "\\");
        } while (FindNextFileA(find, &entry));
        FindClose(find);
    }
    std::vector<Save> best;
    for (const std::string &folder : folders) {
        std::vector<Save> saves;
        find = FindFirstFileA((folder + "*.sav").c_str(), &entry);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            std::string name = entry.cFileName;
            name = name.substr(0, name.size() - 4);
            Save save;
            if (!SafeSaveName(name) || !ParseSaveHeader(ReadFile(folder + entry.cFileName, 8192), save.map, save.played))
                continue;
            save.name = name;
            save.autosave = name.find("autosave") != std::string::npos;
            save.time = (static_cast<std::uint64_t>(entry.ftLastWriteTime.dwHighDateTime) << 32) | entry.ftLastWriteTime.dwLowDateTime;
            save.when = LocalDateTime(entry.ftLastWriteTime);
            saves.push_back(std::move(save));
        } while (FindNextFileA(find, &entry));
        FindClose(find);
        std::sort(saves.begin(), saves.end(), [](const Save &a, const Save &b) { return a.time > b.time; });
        if (!saves.empty() && (best.empty() || saves.front().time > best.front().time)) best = std::move(saves);
    }
    return best;
}

// Portal 2 names a manual save after the second it was made.
inline std::string NewSaveName()
{
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const std::uint64_t ticks = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return std::to_string((ticks - 116444736000000000ull) / 10000000ull);
}

} // namespace VrMenuData
