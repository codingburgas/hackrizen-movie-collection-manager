#include "ui_anim.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace mcm::presentation::anim {

namespace {

float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

ImU32 packRGBA(int r, int g, int b, int a) {
    return IM_COL32(r, g, b, a);
}

void unpackRGBA(ImU32 c, int& r, int& g, int& b, int& a) {
    r = static_cast<int>((c >> IM_COL32_R_SHIFT) & 0xFF);
    g = static_cast<int>((c >> IM_COL32_G_SHIFT) & 0xFF);
    b = static_cast<int>((c >> IM_COL32_B_SHIFT) & 0xFF);
    a = static_cast<int>((c >> IM_COL32_A_SHIFT) & 0xFF);
}

} // namespace

float easeOutCubic(float t) {
    t = clamp01(t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float easeInOutQuad(float t) {
    t = clamp01(t);
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

void setEased(Eased& e, float target) {
    e.target = target;
}

void snapEased(Eased& e, float value) {
    e.current = value;
    e.target  = value;
}

float tickEased(Eased& e, float dtSeconds, float halfLife) {
    if (halfLife <= 0.0f || dtSeconds <= 0.0f) {
        e.current = e.target;
        return e.current;
    }
    const float k = 1.0f - std::exp(-dtSeconds / halfLife);
    e.current += (e.target - e.current) * k;
    if (std::fabs(e.target - e.current) < 0.0005f) {
        e.current = e.target;
    }
    return e.current;
}

float tickHover(HoverLedger& h, ImGuiID id, bool hovered, float dtSeconds,
                float halfLife) {
    float& v = h.values[id];
    const float target = hovered ? 1.0f : 0.0f;
    if (halfLife <= 0.0f || dtSeconds <= 0.0f) {
        v = target;
        return v;
    }
    const float k = 1.0f - std::exp(-dtSeconds / halfLife);
    v += (target - v) * k;
    v = clamp01(v);
    return v;
}

ImU32 lerpColor(ImU32 a, ImU32 b, float t) {
    t = clamp01(t);
    int ar, ag, ab, aa, br, bg, bb, ba;
    unpackRGBA(a, ar, ag, ab, aa);
    unpackRGBA(b, br, bg, bb, ba);
    const int r = static_cast<int>(ar + (br - ar) * t);
    const int g = static_cast<int>(ag + (bg - ag) * t);
    const int bl = static_cast<int>(ab + (bb - ab) * t);
    const int al = static_cast<int>(aa + (ba - aa) * t);
    return packRGBA(r, g, bl, al);
}

ImU32 withAlpha(ImU32 color, float alpha01) {
    int r, g, b, a;
    unpackRGBA(color, r, g, b, a);
    const int na = static_cast<int>(clamp01(alpha01) * 255.0f);
    return packRGBA(r, g, b, na);
}

void drawVerticalGradient(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                          ImU32 top, ImU32 bot, float rounding) {
    if (rounding <= 0.0f) {
        dl->AddRectFilledMultiColor(mn, mx, top, top, bot, bot);
        return;
    }
    // Rounded gradient: clip to rounded path, draw gradient inside.
    dl->PushClipRect(mn, mx, true);
    dl->AddRectFilled(mn, mx, top, rounding);
    // Vertical strip approximation (8 bands) for the gradient blend without
    // an offscreen buffer. Cheap and looks fine for a 100px-tall card.
    const int bands = 24;
    for (int i = 0; i < bands; ++i) {
        const float t0 = static_cast<float>(i) / bands;
        const float t1 = static_cast<float>(i + 1) / bands;
        const ImVec2 a(mn.x, mn.y + (mx.y - mn.y) * t0);
        const ImVec2 b(mx.x, mn.y + (mx.y - mn.y) * t1);
        const ImU32 c0 = lerpColor(top, bot, t0);
        const ImU32 c1 = lerpColor(top, bot, t1);
        dl->AddRectFilledMultiColor(a, b, c0, c0, c1, c1);
    }
    dl->PopClipRect();
}

void drawHorizontalGradient(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                            ImU32 lft, ImU32 rgt, float rounding) {
    if (rounding <= 0.0f) {
        dl->AddRectFilledMultiColor(mn, mx, lft, rgt, rgt, lft);
        return;
    }
    dl->PushClipRect(mn, mx, true);
    dl->AddRectFilled(mn, mx, lft, rounding);
    dl->AddRectFilledMultiColor(mn, mx, lft, rgt, rgt, lft);
    dl->PopClipRect();
}

void drawCard(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
              ImU32 fill, ImU32 highlight, ImU32 shadow,
              ImU32 accent, float rounding, float hover01) {
    // Drop shadow underneath (offset 4px down, expanded 2px sideways).
    const ImVec2 shadowMn(mn.x - 1.0f, mn.y + 3.0f);
    const ImVec2 shadowMx(mx.x + 1.0f, mx.y + 6.0f);
    dl->AddRectFilled(shadowMn, shadowMx, shadow, rounding + 2.0f);

    // Card body fill.
    dl->AddRectFilled(mn, mx, fill, rounding);

    // Top highlight (1.5px tall).
    const ImVec2 hlMn(mn.x + rounding * 0.5f, mn.y);
    const ImVec2 hlMx(mx.x - rounding * 0.5f, mn.y + 1.5f);
    dl->AddRectFilled(hlMn, hlMx, highlight, 0.0f);

    // Hover glow border eases in.
    if (hover01 > 0.001f) {
        const ImU32 glow = withAlpha(accent, hover01 * 0.85f);
        dl->AddRect(mn, mx, glow, rounding, 0, 1.5f);
    }
}

void drawGlassPanel(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                    ImU32 baseFill, ImU32 highlight, ImU32 border,
                    float rounding) {
    dl->AddRectFilled(mn, mx, baseFill, rounding);
    const ImVec2 hlMn(mn.x + rounding * 0.5f, mn.y);
    const ImVec2 hlMx(mx.x - rounding * 0.5f, mn.y + 1.0f);
    dl->AddRectFilled(hlMn, hlMx, highlight, 0.0f);
    dl->AddRect(mn, mx, border, rounding, 0, 1.0f);
}

void drawDonut(ImDrawList* dl, ImVec2 center, float radius, float thickness,
               const float* fractions, const ImU32* colors,
               const float* dim01, int count, float spinPhase) {
    if (count <= 0 || radius <= 0.0f) {
        return;
    }
    const int totalSegments = 96;
    float angle = -3.14159265358979323846f * 0.5f + spinPhase;

    // Track ring (background)
    const ImU32 trackCol = IM_COL32(255, 255, 255, 18);
    dl->PathClear();
    dl->PathArcTo(center, radius, 0.0f, 2.0f * 3.14159265358979323846f, totalSegments);
    dl->PathStroke(trackCol, ImDrawFlags_None, thickness);

    for (int i = 0; i < count; ++i) {
        const float frac = fractions[i];
        if (frac <= 0.0001f) {
            continue;
        }
        const float startA = angle;
        const float endA   = angle + 2.0f * 3.14159265358979323846f * frac;
        const int   segs   = std::max(4, static_cast<int>(totalSegments * frac));
        ImU32 col = colors[i];
        if (dim01 != nullptr) {
            col = withAlpha(col, dim01[i]);
        }
        dl->PathClear();
        dl->PathArcTo(center, radius, startA, endA, segs);
        dl->PathStroke(col, ImDrawFlags_None, thickness);
        angle = endA + 0.02f; // tiny gap between segments for crispness
    }
}

void drawHBar(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
              float fill01, ImU32 trackFill, ImU32 colorA, ImU32 colorB,
              float rounding) {
    fill01 = clamp01(fill01);
    dl->AddRectFilled(mn, mx, trackFill, rounding);
    if (fill01 <= 0.0001f) {
        return;
    }
    const float w = (mx.x - mn.x) * fill01;
    const ImVec2 fillMx(mn.x + w, mx.y);
    if (w < rounding * 2.0f) {
        // Tiny fill: use a single solid color to avoid corner artifacts.
        dl->AddRectFilled(mn, fillMx, colorA, rounding);
    } else {
        // Clip to rounded shape and gradient-fill horizontally.
        dl->PushClipRect(mn, mx, true);
        dl->AddRectFilled(mn, fillMx, colorA, rounding);
        const ImU32 mid = lerpColor(colorA, colorB, fill01);
        dl->AddRectFilledMultiColor(mn, fillMx, colorA, mid, mid, colorA);
        dl->PopClipRect();
    }
}

void drawSparkline(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                   const float* values, int count,
                   ImU32 lineColor, ImU32 fillColor) {
    if (count < 2) {
        return;
    }
    float vmin = values[0], vmax = values[0];
    for (int i = 1; i < count; ++i) {
        vmin = std::min(vmin, values[i]);
        vmax = std::max(vmax, values[i]);
    }
    const float range = std::max(1e-3f, vmax - vmin);
    const float w = mx.x - mn.x;
    const float h = mx.y - mn.y;

    // Build polyline points.
    ImVector<ImVec2> pts;
    pts.reserve(count);
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / (count - 1);
        const float n = (values[i] - vmin) / range;
        pts.push_back(ImVec2(mn.x + w * t, mx.y - h * (0.15f + 0.7f * n)));
    }

    // Filled area beneath the line.
    dl->PathClear();
    dl->PathLineTo(ImVec2(pts[0].x, mx.y));
    for (int i = 0; i < pts.Size; ++i) {
        dl->PathLineTo(pts[i]);
    }
    dl->PathLineTo(ImVec2(pts[pts.Size - 1].x, mx.y));
    dl->PathFillConvex(fillColor);

    // Line on top.
    dl->AddPolyline(pts.Data, pts.Size, lineColor, ImDrawFlags_None, 1.5f);
}

} // namespace mcm::presentation::anim
