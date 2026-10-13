#include "controller_prompts.hpp"

#include "controller_glyphs.hpp"
#include "game_memory.hpp"
#include "glyph_assets.hpp"
#include "mod_options.hpp"
#include "prompt_templates.hpp"

#include <cstring>
#include <string>

// Match the native token callback's controller-text flag and primary settings.
static void* prompt_settings() {
    if (!mod_options.controller_prompts || !glyph_assets::ready)
        return nullptr;
    void* services = *reinterpret_cast<void**>(0x76c588);
    void* talk = services ? field<void*>(services, 0x10) : nullptr;
    void* engine = *reinterpret_cast<void**>(0x76c924);
    if (!engine || !talk || field<unsigned>(talk, 0x4c) != 1)
        return nullptr;
    void* settings = static_cast<Byte*>(engine) + 0x604;
    if (!field<unsigned>(settings, 0x1cc))
        return nullptr;
    return settings;
}

static const char* controller_prompt(void* settings, unsigned action) {
    if (action == 20) {
        for (unsigned i = 6; i <= 9; ++i)
            if (field<int>(settings, 0x2d4 + i * 4) != 0x20)
                return nullptr;
        return glyphs[10];
    }
    if (action >= 20)
        return nullptr;
    const int binding = field<int>(settings, 0x2d4 + action * 4);
    if (binding == -1)
        return "Unbound";
    static constexpr const char* buttons[]{
        "\xee\x80\x80", "\xee\x80\x81", "\xee\x80\x82", "\xee\x80\x83",       "\xee\x80\x84",
        "\xee\x80\x85", "\xee\x80\x86", "\xee\x80\x87", "\xee\x80\x88 click", "\xee\x80\x89 click"};
    if (binding >= 0x30 && binding <= 0x39)
        return buttons[binding - 0x30];
    if (binding == 58)
        return glyphs[11];
    if (binding == 59)
        return glyphs[12];
    if (binding == 0x20) {
        static constexpr const char* directions[]{"\xee\x80\x8a Up", "\xee\x80\x8a Down",
                                                  "\xee\x80\x8a Left", "\xee\x80\x8a Right"};
        return action >= 6 && action <= 9 ? directions[action - 6] : glyphs[10];
    }
    if (binding == 0x14 && action == 16)
        return glyphs[11];
    if (binding == 0x14 && action == 17)
        return glyphs[12];
    if (binding == 0 || binding == 4)
        return glyphs[8];
    if (binding == 12 || binding == 16)
        return glyphs[9];
    return nullptr;
}

// Replay the original SEH prologue before entering the untouched callback.
static void __declspec(naked) original_prompt_token() {
    __asm {
        push -1
        push 0x6c5108
        push 0x4735d7
        ret
    }
}

void __fastcall prompt_token(void* talk, void*, void* token, void* output) {
    void* settings = prompt_settings();
    const unsigned code = field<unsigned>(token, 0x10);
    // Codes in the installed stringtokens.2da; values are physical action slots.
    static constexpr unsigned actions[]{10, 11, 15, 12, 14, 4,  5,  6,  7,  8, 9,
                                        1,  1,  0,  0,  18, 17, 13, 19, 16, 17};
    const char* label = nullptr;
    if (settings && code >= 23 && code <= 43) {
        label = controller_prompt(settings, actions[code - 23]);
        if (label && code >= 34 && code <= 37 && std::strcmp(label, glyphs[8]) == 0) {
            static constexpr const char* directions[]{"\xee\x80\x88 Up", "\xee\x80\x88 Down",
                                                      "\xee\x80\x88 Left", "\xee\x80\x88 Right"};
            label = directions[code - 34];
        }
    }
    if (label) {
        using Assign = void*(__thiscall*)(void*, const char*);
        reinterpret_cast<Assign>(0x6537b0)(output, label);
        return;
    }
    reinterpret_cast<void(__thiscall*)(void*, void*, void*)>(&original_prompt_token)(talk, token,
                                                                                     output);
}

// The shared TLK entry path covers GUI labels, loading tips, style descriptions
// and all tutorial variants. Call through first to preserve lookup errors, token
// expansion, voice metadata and caching. Never edit the cached source string.
static void __declspec(naked) original_prompt_text() {
    __asm {
        push -1
        push 0x6dcdb8
        push 0x65a187
        ret
    }
}

unsigned __fastcall prompt_text(void* talk, void*, unsigned id, void* output, unsigned expand,
                                unsigned variant) {
    using Original = unsigned(__thiscall*)(void*, unsigned, void*, unsigned, unsigned);
    const unsigned result =
        reinterpret_cast<Original>(&original_prompt_text)(talk, id, output, expand, variant);
    void* settings = prompt_settings();
    if (!result || !settings)
        return result;
    std::string text;
    if (id >= 130608 && id <= 130617) {
        static constexpr const char* buttons[]{
            "\xee\x80\x80",       "\xee\x80\x81",      "\xee\x80\x82", "\xee\x80\x83",
            "\xee\x80\x84",       "\xee\x80\x85",      "\xee\x80\x86", "\xee\x80\x87",
            "\xee\x80\x88 click", "\xee\x80\x89 click"};
        text = buttons[id - 130608];
    } else if (id == 130618 || id == 130619)
        text = glyphs[id == 130618 ? 11 : 12];
    else if (id == 130695)
        text = glyphs[10];
    else if (mod_options.right_stick_target_switching && id == 85242)
        text = std::string("While locked on in combat, flick ") + glyphs[9] +
               " left or right to switch targets. Let the stick return to center between flicks. "
               "You can also use the target buttons.";
    else if (id == 5664 || id == 129422 || id == 6174 || id == 104773 || id == 122686 ||
             id == 122691) {
        // Preserve the dynamically substituted save name in these error prompts.
        const char* current = field<const char*>(output, 0);
        if (!current)
            return result;
        text = current;
        const auto pos = text.find("Press OK");
        const char* button = controller_prompt(settings, 10);
        if (pos == std::string::npos || !button)
            return result;
        text.replace(pos, 8, std::string("Press ") + button);
    } else if ((id == 85236 || id == 121792) && field<int>(settings, 0x2d4 + 18 * 4) == -1) {
        const char* left = controller_prompt(settings, 16);
        const char* right = controller_prompt(settings, 17);
        if (!left || !right || field<int>(settings, 0x2d4 + 16 * 4) == -1 ||
            field<int>(settings, 0x2d4 + 17 * 4) == -1)
            text = "No Target is unassigned. Set it in Controller Configuration.";
        else if (id == 85236)
            text = std::string("Press ") + left + " and " + right +
                   " together to enter Free Target mode. Press either or both again to exit.";
        else
            text = std::string("Press ") + left + " or " + right + " to exit";
    } else {
        const char* source = nullptr;
        for (const auto& entry : prompt_templates)
            if (entry.id == id) {
                source = entry.text;
                break;
            }
        if (!source)
            return result;
        for (const char* p = source; *p;) {
            if (*p != '{') {
                text += *p++;
                continue;
            }
            ++p;
            unsigned action = 0;
            while (*p >= '0' && *p <= '9')
                action = action * 10 + static_cast<unsigned>(*p++ - '0');
            const char* label = controller_prompt(settings, action);
            if (*p != '}' || !label)
                return result;
            ++p;
            text += label;
        }
    }
    reinterpret_cast<void*(__thiscall*)(void*, const char*)>(0x6537b0)(output, text.c_str());
    return result;
}
