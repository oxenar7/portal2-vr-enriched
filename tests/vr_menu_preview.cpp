// Offline preview of the in-world VR menu: runs the mod's own menu model and painter on the
// installed game's texts and save files and writes PNG files, so the look and the content
// can be judged without a headset.
//   vr_menu_preview.exe <output folder> [<Portal 2 folder with a trailing backslash>]
#include "../L4D2VR/vr_menu_model.h"
#include <gdiplus.h>
#include <chrono>
#include <cstdio>
#include <string>

using namespace VrMenuPaint;

static bool SavePng(const std::wstring &path, const std::uint32_t *pixels, int width, int height)
{
    Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppARGB,
                           reinterpret_cast<BYTE *>(const_cast<std::uint32_t *>(pixels)));
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    std::vector<BYTE> buffer(bytes);
    auto *codecs = reinterpret_cast<Gdiplus::ImageCodecInfo *>(buffer.data());
    Gdiplus::GetImageEncoders(count, bytes, codecs);
    for (UINT i = 0; i < count; ++i)
        if (std::wstring(codecs[i].MimeType) == L"image/png")
            return bitmap.Save(path.c_str(), &codecs[i].Clsid, nullptr) == Gdiplus::Ok;
    return false;
}

// A stand-in for the paused, dimmed test chamber behind the panels.
static void Backdrop(Canvas &scene)
{
    for (int y = 0; y < scene.Height(); ++y)
        for (int x = 0; x < scene.Width(); ++x) {
            int v = 46;
            if ((x / 240 + y / 240) % 2) v += 9;   // wall tiles
            if (x % 240 < 3 || y % 240 < 3) v -= 10; // seams
            const auto c = static_cast<std::uint8_t>(std::clamp(v, 0, 255));
            scene.Blend(x, y, {c, static_cast<std::uint8_t>(c + 2), static_cast<std::uint8_t>(c + 5), 255});
        }
}

static void Compose(Canvas &scene, const Canvas &panel, int left, int top)
{
    for (int y = 0; y < panel.Height(); ++y)
        for (int x = 0; x < panel.Width(); ++x) {
            const std::uint32_t p = panel.Data()[static_cast<std::size_t>(y) * panel.Width() + x];
            scene.Blend(left + x, top + y, {static_cast<std::uint8_t>(p >> 16), static_cast<std::uint8_t>(p >> 8),
                                           static_cast<std::uint8_t>(p), static_cast<std::uint8_t>(p >> 24)});
        }
}

static double g_PaintMs = 0.0;
static int g_Painted = 0;

// One state of the menu: its panels side by side, tops aligned, as they hang in the headset.
static void State(Canvas &scene, Text &text, const VrMenu::Model &model, const ConfigSnapshot &config, int top)
{
    const int widths[] = {560, 560, 700}, lefts[] = {30, 610, 1190};
    for (int column = 0; column < model.Columns(); ++column) {
        const Panel panel = model.Panel(column, config);
        Canvas canvas(widths[column], ContentHeight(panel, widths[column]));
        const auto start = std::chrono::steady_clock::now();
        const Layout layout = Arrange(panel, canvas.Width(), canvas.Height(), text);
        Paint(canvas, text, panel, layout);
        Glow(canvas, static_cast<int>(7 * layout.scale), 0.55f);
        g_PaintMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        ++g_Painted;
        Compose(scene, canvas, lefts[column], top);
    }
}

int wmain(int argc, wchar_t **argv)
{
    const std::wstring folder = argc > 1 ? argv[1] : L".";
    std::string game = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Portal 2\\";
    if (argc > 2) {
        game.clear();
        for (const wchar_t *c = argv[2]; *c; ++c) game += static_cast<char>(*c);
    }
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &startup, nullptr);
    {
        Text text(1024, 256);
        VrMenuData::Strings strings;
        strings.Load(game);
        VrMenu::Texts texts;
        texts.Load(strings);
        const auto saves = VrMenuData::ListSaves(game);
        std::wprintf(L"font: %ls; language: %hs (%zu texts); saves: %zu\n", text.Family().c_str(),
                     strings.Language().c_str(), strings.Count(), saves.size());

        ConfigSnapshot config;
        config.snapTurning = true;
        config.snapTurnAngle = 45.0f;
        config.experimentalWorldAimMarker = true;
        config.trackingMode = TrackingSpace::TrackingMode::Standing;

        Canvas scene(1920, 2330);
        Backdrop(scene);

        VrMenu::Model model;
        model.SetTexts(texts);
        model.SetSaves(saves);
        model.Open();
        model.Hover(0, VrMenu::Model::RootLoad, -1);
        State(scene, text, model, config, 30);                      // just paused

        model.Click(0, VrMenu::Model::RootLoad, 0, -1, config);
        model.Click(1, 1, 0, -1, config);
        model.Hover(2, -1, 1);
        State(scene, text, model, config, 560);                     // load a game

        model.Click(0, VrMenu::Model::RootOptions, 0, -1, config);
        model.Click(1, 0, 0, -1, config);
        model.Hover(2, 1, -1);
        State(scene, text, model, config, 1130);                    // options, comfort

        model.Click(1, 2, 0, -1, config);
        model.Hover(1, 3, -1);
        State(scene, text, model, config, 1650);                    // options, advanced

        const bool saved = SavePng(folder + L"\\menu_preview.png", scene.Data(), scene.Width(), scene.Height());
        std::wprintf(L"%ls; %.1f ms per panel\n", saved ? L"menu_preview.png written" : L"could not write the preview",
                     g_Painted ? g_PaintMs / g_Painted : 0.0);
    }
    Gdiplus::GdiplusShutdown(token);
    return 0;
}
