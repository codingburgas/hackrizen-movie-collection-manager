/**
 * ui_anim - small free-function helpers powering the cinematic UI:
 * easing, eased scalars, hover ledger, and ImDrawList draw primitives
 * (cards, gradients, donut, horizontal bar, sparkline).
 *
 * Strict structural style: no classes. Plain structs + free functions.
 */
#ifndef MCM_UI_ANIM_H
#define MCM_UI_ANIM_H

#include <cstdint>
#include <unordered_map>

#include <imgui.h>

namespace mcm::presentation::anim {

float easeOutCubic(float t);
float easeInOutQuad(float t);

/**
 * Eased - exponential-decay tween toward a target value.
 * Frame-rate independent: tick() applies (1 - exp(-dt/halfLife)) per frame.
 */
struct Eased {
    float current = 0.0f;
    float target  = 0.0f;
};

void  setEased(Eased& e, float target);
void  snapEased(Eased& e, float value);
float tickEased(Eased& e, float dtSeconds, float halfLife = 0.18f);

/**
 * HoverLedger - tracks a per-id hover progress value in [0, 1] eased toward
 * 1 while hovered and toward 0 otherwise.
 */
struct HoverLedger {
    std::unordered_map<ImGuiID, float> values;
};

float tickHover(HoverLedger& h, ImGuiID id, bool hovered, float dtSeconds,
                float halfLife = 0.10f);

/**
 * drawVerticalGradient - filled rounded rect with a top->bottom color blend.
 * Uses 32-bit ImU32 colors. Rounding may be 0.
 */
void drawVerticalGradient(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                          ImU32 top, ImU32 bot, float rounding);

/**
 * drawHorizontalGradient - filled rounded rect with a left->right blend.
 */
void drawHorizontalGradient(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                            ImU32 lft, ImU32 rgt, float rounding);

/**
 * drawCard - rounded card with a subtle top highlight + bottom shadow line
 * and an optional hover-progress glow border. hover01 in [0,1].
 */
void drawCard(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
              ImU32 fill, ImU32 highlight, ImU32 shadow,
              ImU32 accent, float rounding, float hover01);

/**
 * drawGlassPanel - fake glassmorphism: layered semi-transparent fill +
 * top inner highlight + 1px border. No actual blur.
 */
void drawGlassPanel(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                    ImU32 baseFill, ImU32 highlight, ImU32 border,
                    float rounding);

/**
 * drawDonut - animated multi-segment donut. fractions sum to <= 1.
 * spinPhase is a slow rotation in radians. dim01[i] in [0,1] dims segment i
 * (e.g. on hover of another segment).
 */
void drawDonut(ImDrawList* dl, ImVec2 center, float radius, float thickness,
               const float* fractions, const ImU32* colors,
               const float* dim01, int count, float spinPhase);

/**
 * drawHBar - horizontal bar with gradient fill (colorA -> colorB).
 * Bar height = mx.y - mn.y; fill01 in [0,1] controls width.
 */
void drawHBar(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
              float fill01, ImU32 trackFill, ImU32 colorA, ImU32 colorB,
              float rounding);

/**
 * drawSparkline - thin filled+lined sparkline. values are normalized to the
 * box's height by min/max of the input. count must be >= 2.
 */
void drawSparkline(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                   const float* values, int count,
                   ImU32 lineColor, ImU32 fillColor);

/**
 * lerpColor - linear interpolation between two ImU32 RGBA colors.
 */
ImU32 lerpColor(ImU32 a, ImU32 b, float t);

/**
 * withAlpha - replace the alpha channel of a packed color.
 */
ImU32 withAlpha(ImU32 color, float alpha01);

} // namespace mcm::presentation::anim

#endif // MCM_UI_ANIM_H
