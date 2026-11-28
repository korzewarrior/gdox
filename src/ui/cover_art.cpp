#include "ui/cover_art.hpp"

#include "app/app.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
/* Declared here: <windows.h> clashes with raylib (Rectangle, LoadImage...). */
extern "C" {
__declspec(dllimport) long __stdcall URLDownloadToFileA(
    void *, const char *, const char *, unsigned long, void *
);
__declspec(dllimport) long __stdcall CoInitializeEx(void *, unsigned long);
__declspec(dllimport) void __stdcall CoUninitialize(void);
}
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace gdox::ui::cover {
namespace {

struct slot {
    std::string key;
    std::string base;
    Texture2D texture{};
    std::shared_ptr<std::atomic<bool>> fetched;
    bool settled = false;
};

slot current;

/* Where the art lives, best first. Xbox covers come from xemu's title
 * database, Xbox 360 covers from the 360 Marketplace, GameCube and Wii
 * covers from GameTDB (the same source sLaunch uses). */
std::vector<std::string> sources(gdox_media_platform platform, const std::string &id)
{
    std::vector<std::string> urls;

    if (platform == GDOX_MEDIA_PLATFORM_XBOX && id.size() == 8U) {
        /* titles/<publisher letters>/<title number, three digits or more>/ */
        const unsigned long title = std::strtoul(id.c_str(), nullptr, 16);
        const int first = static_cast<int>((title >> 24U) & 0xFFU);
        const int second = static_cast<int>((title >> 16U) & 0xFFU);
        char folder[32];

        if (std::isalnum(first) == 0 || std::isalnum(second) == 0) {
            return urls;
        }
        (void)std::snprintf(
            folder, sizeof(folder), "%c%c/%03lu",
            first, second, title & 0xFFFFUL
        );
        for (const char *extension : {".jpg", ".png"}) {
            urls.push_back(
                std::string("https://raw.githubusercontent.com/xemu-project/xdb/master/titles/")
                + folder + "/cover_front" + extension
            );
        }
    } else if (platform == GDOX_MEDIA_PLATFORM_XBOX_360 && id.size() == 8U) {
        urls.push_back(
            "https://download-ssl.xbox.com/content/images/66acd000-77fe-1000-9115-d802"
            + id + "/1033/boxartlg.jpg"
        );
    } else if ((platform == GDOX_MEDIA_PLATFORM_GAMECUBE
                   || platform == GDOX_MEDIA_PLATFORM_WII)
        && id.size() == 6U) {
        /* The fourth letter of a game ID is its region. */
        const char *own = "EN";
        switch (id[3]) {
        case 'E': own = "US"; break;
        case 'J': own = "JA"; break;
        case 'K': own = "KO"; break;
        case 'D': own = "DE"; break;
        case 'F': own = "FR"; break;
        case 'S': own = "ES"; break;
        case 'I': own = "IT"; break;
        default: break;
        }
        for (const char *region : {own, "US", "EN", "JA"}) {
            std::string url = std::string("https://art.gametdb.com/wii/cover/")
                + region + "/" + id + ".png";
            if (std::find(urls.begin(), urls.end(), url) == urls.end()) {
                urls.push_back(std::move(url));
            }
        }
    }
    return urls;
}

bool download(const std::string &url, const std::string &path)
{
#ifdef _WIN32
    return URLDownloadToFileA(
        nullptr, url.c_str(), path.c_str(), 0UL, nullptr
    ) >= 0L;
#else
    char *const arguments[] = {
        const_cast<char *>("curl"),
        const_cast<char *>("-fsL"),
        const_cast<char *>("--max-time"),
        const_cast<char *>("30"),
        const_cast<char *>("-o"),
        const_cast<char *>(path.c_str()),
        const_cast<char *>(url.c_str()),
        nullptr,
    };
    pid_t child = 0;
    int status = 0;

    if (posix_spawnp(&child, "curl", nullptr, nullptr, arguments, environ) != 0) {
        return false;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

bool is_image(const std::string &path)
{
    unsigned char magic[2] = {};
    std::FILE *file = std::fopen(path.c_str(), "rb");

    if (file == nullptr) {
        return false;
    }
    const std::size_t bytes = std::fread(magic, 1U, sizeof(magic), file);
    (void)std::fclose(file);
    return bytes == sizeof(magic)
        && ((magic[0] == 0x89U && magic[1] == 'P')
            || (magic[0] == 0xFFU && magic[1] == 0xD8U));
}

void fetch(
    std::vector<std::string> urls,
    std::string base,
    std::shared_ptr<std::atomic<bool>> done
)
{
    static std::atomic<unsigned int> serial{0U};
    const std::string part = base + "." + std::to_string(serial++) + ".part";
    std::error_code ignored;

#ifdef _WIN32
    const bool com = CoInitializeEx(nullptr, 0x2UL /* apartment */) >= 0L;
#endif
    std::filesystem::create_directories(
        std::filesystem::path(base).parent_path(), ignored
    );
    for (const std::string &url : urls) {
        const std::string path = base + url.substr(url.size() - 4U);

        if (download(url, part) && is_image(part)
            && std::rename(part.c_str(), path.c_str()) == 0) {
            break;
        }
    }
    (void)std::remove(part.c_str());
#ifdef _WIN32
    if (com) {
        CoUninitialize();
    }
#endif
    done->store(true);
}

bool load_cached(slot &target)
{
    for (const char *extension : {".jpg", ".png"}) {
        const std::string path = target.base + extension;

        if (!FileExists(path.c_str())) {
            continue;
        }
        Image image = LoadImage(path.c_str());
        if (image.data != nullptr && image.height > 512) {
            /* Shown about 250 pixels tall; keep twice that for HiDPI. */
            ImageResize(&image, image.width * 512 / image.height, 512);
        }
        target.texture = LoadTextureFromImage(image);
        UnloadImage(image);
        if (target.texture.id != 0U) {
            GenTextureMipmaps(&target.texture);
            SetTextureFilter(target.texture, TEXTURE_FILTER_TRILINEAR);
            return true;
        }
        /* Unreadable: drop it so the next start fetches it again. */
        (void)std::remove(path.c_str());
    }
    return false;
}

}

const Texture2D *for_disc(gdox_media_platform platform, const char *media_id)
{
    const char *prefix = platform == GDOX_MEDIA_PLATFORM_XBOX ? "xbox-"
        : platform == GDOX_MEDIA_PLATFORM_XBOX_360 ? "x360-"
        : platform == GDOX_MEDIA_PLATFORM_GAMECUBE
            || platform == GDOX_MEDIA_PLATFORM_WII ? "gc-"
        : nullptr;
    const std::string id = media_id != nullptr ? media_id : "";
    const std::string key = prefix != nullptr && !id.empty()
        ? prefix + id : std::string();

    if (key != current.key) {
        shutdown();
        current.key = key;
        char directory[4096];
        if (key.empty()
            || !gdox_app_data_path("covers", directory, sizeof(directory))) {
            current.settled = true;
        } else {
            current.base = std::string(directory) + "/" + key;
        }
    }
    if (current.texture.id != 0U) {
        return &current.texture;
    }
    if (current.settled) {
        return nullptr;
    }
    if (current.fetched == nullptr) {
        if (load_cached(current)) {
            return &current.texture;
        }
        current.fetched = std::make_shared<std::atomic<bool>>(false);
        std::thread(fetch, sources(platform, id), current.base, current.fetched)
            .detach();
        return nullptr;
    }
    if (current.fetched->load()) {
        current.settled = true;
        if (load_cached(current)) {
            return &current.texture;
        }
    }
    return nullptr;
}

void shutdown()
{
    if (current.texture.id != 0U) {
        UnloadTexture(current.texture);
    }
    current = slot{};
}

}
