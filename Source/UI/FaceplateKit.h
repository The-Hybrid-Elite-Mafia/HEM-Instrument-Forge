#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pg::ui
{
/**
    Cuts the pieces of Steven's main faceplate (Assets/Faceplates/Main-Faceplate.png, 3840 x 2160)
    and draws them at any size with 9-slice scaling, so every window (main screen, plugin editors)
    is built from the same chrome, glass, sockets and lightning frame.

    Source rectangles are in 4K pixels of the main faceplate. Nothing interactive is painted in the
    art: knobs, text, LEDs and meters are drawn in code on top of the empty sockets and glass.

    Message thread only. The full-size image is decoded once; half and quarter size copies are made
    with 2:1 box filtering so heavy downscaling stays smooth (no shimmering on the brushed metal).
*/
class FaceplateKit
{
public:
    struct Piece
    {
        juce::Rectangle<int> src;      // in the 4K faceplate
        juce::BorderSize<int> insets;  // corners / edges that keep their shape (top, left, bottom, right)
        float clipRadius;               // > 0: clip to a rounded rectangle (source px) so no square corners show
    };

    // Measured on Main-Faceplate.png
    static inline const Piece frame      { { 0, 0, 3840, 2160 },    { 50, 50, 56, 50 } };
    static inline const Piece titleStrip { { 52, 28, 3733, 178 },   { 60, 250, 60, 70 } };   // logo socket on the left
    static inline const Piece chromeStrip{ { 52, 1962, 3733, 148 }, { 50, 70, 50, 70 } };   // plain brushed chrome
    static inline const Piece gap        { { 52, 196, 3733, 30 },   { 0, 0, 0, 0 } };        // dark line + blue glow
    static inline const Piece plate      { { 95, 240, 1117, 209 },  { 60, 60, 60, 60 } };    // brushed plate in a bezel
    static inline const Piece glass      { { 2626, 238, 1118, 210 },{ 50, 50, 50, 50 }, 30.0f };    // dark display glass
    static inline const Piece socket     { { 1302, 247, 196, 196 }, { 0, 0, 0, 0 } };        // round chrome socket
    static inline const Piece tabSlot    { { 96, 495, 898, 193 },   { 55, 55, 55, 55 } };    // grey tab slot
    static inline const Piece panel      { { 722, 684, 3048, 1264 },{ 60, 60, 60, 60 } };    // grey work panel
    static inline const Piece sidePanel  { { 30, 684, 700, 1264 },  { 60, 60, 60, 60 } };    // left grey panel

    /** Logo socket centre / radius inside titleStrip, in source pixels relative to the piece. */
    static constexpr float kLogoCx = 106.0f, kLogoCy = 97.0f, kLogoR = 58.0f;

    static FaceplateKit& get();

    bool isLoaded() const noexcept { return mips[0].isValid(); }
    const juce::Image& fullImage() const noexcept { return mips[0]; }

    /** Draws a piece into dest. `scale` = destination pixels per source pixel for the insets
        (pass the same scale for every piece of a window so bezels match). */
    void draw (juce::Graphics&, const Piece&, juce::Rectangle<float> dest, float scale) const;
    /** The round chrome socket, clipped to its ring (no square of chrome around it). */
    void drawSocket (juce::Graphics&, juce::Rectangle<float> dest) const;
    /** Only the frame border (the inside is left alone). */
    void drawFrameBorder (juce::Graphics&, juce::Rectangle<float> dest, float scale) const;

private:
    FaceplateKit();
    const juce::Image& mipFor (float scale, float& mipScale) const;
    void drawRegion (juce::Graphics&, const juce::Image&, float mipScale, juce::Rectangle<float> src, juce::Rectangle<float> dst) const;

    std::array<juce::Image, 3> mips;   // 1, 1/2, 1/4
};
} // namespace pg::ui
