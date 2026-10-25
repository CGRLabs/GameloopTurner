#include "glt/ui/widgets.hpp"

#include "glt/ui/app.hpp"

#include <algorithm>
#include <cstdio>

namespace glt::ui::widgets {
namespace {

constexpr float kLabelW = 132.0f;

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

// Width available inside the current window minus the label column.
float body_width() {
    return std::max(120.0f, ImGui::GetContentRegionAvail().x);
}

} // namespace

// ── Key/value rows ───────────────────────────────────────────────────────────

void kv_row(std::string_view label, std::string_view value, const ImVec4* value_color) {
    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
    ImGui::TextUnformatted(std::string(label).c_str());
    ImGui::PopStyleColor();

    ImGui::SameLine(kLabelW);

    if (value_color) ImGui::PushStyleColor(ImGuiCol_Text, *value_color);
    ImGui::TextUnformatted(std::string(value).c_str());
    if (value_color) ImGui::PopStyleColor();

    ImGui::PopFont();
}

void kv_row_continued(std::string_view value, const ImVec4* value_color) {
    ImGui::PushFont(fonts::current().small_text);
    ImGui::Dummy(ImVec2(kLabelW, 0));
    ImGui::SameLine(kLabelW);

    if (value_color) ImGui::PushStyleColor(ImGuiCol_Text, *value_color);
    ImGui::TextUnformatted(std::string(value).c_str());
    if (value_color) ImGui::PopStyleColor();

    ImGui::PopFont();
}

void gauge(std::string_view label, float value, float maximum, std::string_view unit,
           float warn_below) {
    const float ratio = (maximum > 0.0f) ? std::clamp(value / maximum, 0.0f, 1.0f) : 0.0f;

    ImGui::PushFont(fonts::current().small_text);

    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
    ImGui::TextUnformatted(std::string(label).c_str());
    ImGui::PopStyleColor();

    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.1f / %.1f %s", static_cast<double>(value),
                  static_cast<double>(maximum), std::string(unit).c_str());

    const float w = ImGui::CalcTextSize(buf).x + 8.0f;
    ImGui::SameLine(std::max(kLabelW, ImGui::GetContentRegionAvail().x + kLabelW - w));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(kLabelW, 0));
    ImGui::SameLine(kLabelW);

    ImVec4 bar = col(theme::kAccent);
    if (warn_below > 0.0f && value < warn_below) {
        bar = (value < warn_below * 0.5f) ? col(theme::kError) : col(theme::kWarn);
    }

    const float bar_h = 8.0f;
    const float bar_w = body_width();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(bar_w, bar_h));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + bar_w, pos.y + bar_h), theme::u32(col(theme::kBorder)), 4.0f);
    dl->AddRectFilled(pos, ImVec2(pos.x + bar_w * ratio, pos.y + bar_h), theme::u32(bar), 4.0f);

    ImGui::PopFont();
}

// ── Fix rows ─────────────────────────────────────────────────────────────────

void fix_checkbox(bool* value, std::string_view label, std::string_view why) {
    ImGui::PushStyleColor(ImGuiCol_CheckMark, col(theme::kAccent));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col(theme::kInput));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col(theme::kBgHover));
    ImGui::Checkbox("##fix", value);
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(std::string(label).c_str());
    ImGui::PopStyleColor();

    // Indent under the checkbox so the explanation lines up with the label,
    // not with the tick box.
    const float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::Dummy(ImVec2(indent, 0));
    ImGui::SameLine(indent);
    body_text(why, body_width() - indent, col(theme::kTextDim));
}

void fix_hero_checkbox(bool* value, std::string_view label, std::string_view why) {
    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kAccent));
    ImGui::TextUnformatted("\xE2\x98\x85  RECOMMENDED"); // ★  RECOMMENDED
    ImGui::PopStyleColor();
    ImGui::PopFont();

    fix_checkbox(value, label, why);
}

// ── Pills and dots ───────────────────────────────────────────────────────────

void status_dot(const ImVec4& color, float diameter) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(diameter + 2.0f, ImGui::GetTextLineHeight()));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 mid(pos.x + 1.0f + diameter * 0.5f,
                     pos.y + ImGui::GetTextLineHeight() * 0.5f);
    dl->AddCircleFilled(mid, diameter * 0.5f, theme::u32(color));
}

void status_pill(std::string_view text, const ImVec4& fg, const ImVec4& bg) {
    ImGui::PushFont(fonts::current().small_text);

    const ImVec2 padding(10.0f, 4.0f);
    const ImVec2 text_sz = ImGui::CalcTextSize(text.data(), text.data() + text.size());
    const ImVec2 size(text_sz.x + padding.x * 2.0f, text_sz.y + padding.y * 2.0f);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), theme::u32(bg), size.y * 0.5f);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + padding.x, pos.y + padding.y));
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y));

    ImGui::PopFont();
}

void level_icon(log::Level level) {
    // log_console.py used ✓ ✗ ⚠ ● ◌ ⇄. The exact glyph per level is preserved;
    // the colour comes from theme::level_color so icon and text always match.
    const char* glyph = "\xE2\x97\x8F"; // ●
    switch (level) {
        case log::Level::Success:  glyph = "\xE2\x9C\x93"; break; // ✓
        case log::Level::Error:    glyph = "\xE2\x9C\x97"; break; // ✗
        case log::Level::Warning:  glyph = "\xE2\x9A\xA0"; break; // ⚠
        case log::Level::Drift:    glyph = "\xE2\x87\x84"; break; // ⇄
        case log::Level::Progress: glyph = "\xE2\x97\x8C"; break; // ◌
        case log::Level::Info:
        default:                   glyph = "\xE2\x97\x8F"; break; // ●
    }

    // Reserve the glyph's width so the timestamp column below stays aligned.
    ImGui::PushStyleColor(ImGuiCol_Text, theme::level_color(level));
    ImGui::TextUnformatted(glyph);
    ImGui::PopStyleColor();
}

// ── Structure ────────────────────────────────────────────────────────────────

void page_header(std::string_view title, std::string_view intro) {
    ImGui::PushFont(fonts::current().title);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(std::string(title).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    if (!intro.empty()) {
        body_text(intro, body_width(), col(theme::kTextMuted));
    }
}

void body_text(std::string_view text, float width, const ImVec4& color) {
    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(std::max(ImGui::GetCursorPosX() + 10.0f,
                                    ImGui::GetCursorPosX() + width));
    ImGui::TextUnformatted(std::string(text).c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void separator() {
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));
}

void help_marker(const char* desc) {
    ImGui::SameLine();
    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 0.8f));
    ImGui::TextDisabled("(?)");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void tooltip(const char* desc) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool collapsible(std::string_view label, bool* open) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::InvisibleButton("##collapse", avail);
    // IsItemHovered must be asked immediately: any other call advances the
    // "last item" and the hover state would be reported for that instead.
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight();
    const ImVec4 fill = hovered ? col(theme::kBgActive) : col(theme::kBgHover);
    dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + h), theme::u32(fill), 6.0f);

    const char* arrow = *open ? "\xE2\x96\xBE " : "\xE2\x96\xB8 "; // ▾ / ▸
    const std::string text = std::string(arrow) + std::string(label);

    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 10.0f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));

    if (clicked) *open = !*open;
    return *open;
}

bool action_button(std::string_view label, bool enabled, const ImVec4& accent) {
    if (!enabled) ImGui::BeginDisabled();

    ImVec2 size(ImGui::GetContentRegionAvail().x, 32.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kAccentHover));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOnAccent));

    // ImGui::Button has no (label_begin, label_end, size) overload in 1.91.x, so
    // the text is materialised and passed as a NUL-terminated C string.
    const std::string text(label);
    const bool pressed = ImGui::Button(text.c_str(), size);

    ImGui::PopStyleColor(3);
    if (!enabled) ImGui::EndDisabled();
    return pressed;
}

int action_row(std::string_view primary, std::string_view secondary, bool enabled) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = 8.0f;
    const float half = (avail.x - gap) * 0.5f;

    int result = -1;

    if (!enabled) ImGui::BeginDisabled();

    ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kAccent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kAccentHover));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOnAccent));
    const std::string primary_text(primary);
    if (ImGui::Button(primary_text.c_str(), ImVec2(half, 32.0f))) {
        result = 0;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, gap);

    ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    const std::string secondary_text(secondary);
    if (ImGui::Button(secondary_text.c_str(), ImVec2(half, 32.0f))) {
        result = 1;
    }
    ImGui::PopStyleColor(3);

    if (!enabled) ImGui::EndDisabled();
    return result;
}

// ── Log console ──────────────────────────────────────────────────────────────

void log_console(App& app, float height) {
    const auto& entries = app.log_entries();
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const int count = static_cast<int>(entries.size());

    const ImGuiID id = ImGui::GetID("##logbody");
    ImGui::BeginChild(id, ImVec2(0, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(entries.size()));

    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& e = entries[static_cast<std::size_t>(i)];

            if (e.is_group_header) {
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + line_h),
                                  theme::u32(col(theme::kBgCard)), 6.0f);

                ImGui::PushFont(fonts::current().small_text);
                ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kAccent));
                ImGui::TextUnformatted(("\xE2\x94\x80\xE2\x94\x80 " + e.text + " \xE2\x94\x80\xE2\x94\x80").c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                continue;
            }

            level_icon(e.level);
            ImGui::SameLine();

            ImGui::PushFont(fonts::current().mono_small);
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
            ImGui::TextUnformatted(e.timestamp.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, theme::level_color(e.level));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextUnformatted(e.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
    }

    // Auto-scroll pins to the bottom, but only when the user has not scrolled
    // up to read something — forcing the view down while they are reading is
    // the single most annoying thing a log pane can do.
    const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f;
    if (app.log_auto_scroll() && at_bottom && count > 0) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

bool toggle_switch(std::string_view str_id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float height = 22.0f;
    const float width = 42.0f;
    const float radius = height * 0.5f;

    std::string id_str(str_id);
    ImGui::InvisibleButton(id_str.c_str(), ImVec2(width, height));
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();

    if (clicked) {
        *v = !(*v);
    }

    const ImU32 col_bg = *v
        ? (hovered ? theme::u32(col(theme::kAccentHover)) : theme::u32(col(theme::kAccent)))
        : (hovered ? theme::u32(col(theme::kBgHover)) : theme::u32(col(theme::kInput)));

    const ImU32 col_border = *v
        ? theme::u32(col(theme::kAccentHover))
        : (hovered ? theme::u32(col(theme::kTextDim)) : theme::u32(col(theme::kBorder)));

    // Track
    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, radius);
    draw_list->AddRect(p, ImVec2(p.x + width, p.y + height), col_border, radius, 0, 1.2f);

    // Thumb
    const float thumb_radius = radius - 3.5f;
    const float thumb_x = *v ? (p.x + width - radius) : (p.x + radius);
    const float thumb_y = p.y + radius;
    const ImU32 col_thumb = *v ? IM_COL32(16, 16, 20, 255) : theme::u32(col(theme::kTextPrimary));

    draw_list->AddCircleFilled(ImVec2(thumb_x, thumb_y), thumb_radius, col_thumb);

    return clicked;
}

bool setting_row(std::string_view str_id, std::string_view icon, std::string_view label,
                 std::string_view desc, bool* value, std::string_view badge) {
    std::string id_str(str_id);
    ImGui::PushID(id_str.c_str());

    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));

    std::string child_name = std::string("##card_") + id_str;
    ImGui::BeginChild(child_name.c_str(), ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    const float content_w = ImGui::GetContentRegionAvail().x;
    const float toggle_w = 46.0f;
    const float text_w = std::max(100.0f, content_w - toggle_w - 20.0f);

    if (!icon.empty()) {
        ImGui::PushFont(fonts::current().heading);
        ImGui::TextUnformatted(icon.data(), icon.data() + icon.size());
        ImGui::PopFont();
        ImGui::SameLine(0, 12.0f);
    }

    ImGui::BeginGroup();
    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    if (!badge.empty()) {
        ImGui::SameLine(0, 8.0f);
        status_pill(badge, col(theme::kAccent), col(theme::kAccentDim));
    }

    if (!desc.empty()) {
        body_text(desc, text_w, col(theme::kTextMuted));
    }
    ImGui::EndGroup();

    ImGui::SameLine(std::max(0.0f, content_w - toggle_w));
    const bool clicked = toggle_switch("##sw", value);

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    return clicked;
}

void infobar(std::string_view icon, std::string_view title, std::string_view message, const ImVec4* border_col) {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const ImVec4 bar_col = border_col ? *border_col : col(theme::kInfo);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.14f, 0.22f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));

    ImGui::BeginChild("##infobar", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    const ImVec2 win_pos = ImGui::GetWindowPos();
    const ImVec2 win_sz = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + 4.0f, win_pos.y + win_sz.y),
                      theme::u32(bar_col), theme::kCardRadius, ImDrawFlags_RoundCornersLeft);

    if (!icon.empty()) {
        ImGui::TextUnformatted(icon.data(), icon.data() + icon.size());
        ImGui::SameLine(0, 8.0f);
    }
    if (!title.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, bar_col);
        ImGui::TextUnformatted(title.data(), title.data() + title.size());
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8.0f);
    }
    if (!message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
        ImGui::TextUnformatted(message.data(), message.data() + message.size());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

bool chip_button(std::string_view label, bool active) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    const ImVec4 bg = active ? col(theme::kAccentDim) : col(theme::kInput);
    const ImVec4 text = active ? col(theme::kAccent) : col(theme::kTextMuted);
    const ImVec4 border = active ? col(theme::kAccent) : col(theme::kBorder);

    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(theme::kBgActive));
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleColor(ImGuiCol_Border, border);

    std::string label_str(label);
    const bool clicked = ImGui::Button(label_str.c_str());

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    return clicked;
}

} // namespace glt::ui::widgets