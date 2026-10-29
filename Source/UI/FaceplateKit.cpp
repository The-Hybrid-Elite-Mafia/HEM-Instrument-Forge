#include "FaceplateKit.h"
#include "PlaygroundBinaryData.h"

namespace pg::ui
{
namespace
{
    /** Exact 2:1 box downscale (average of 2 x 2 pixels). */
    juce::Image halve (const juce::Image& src)
    {
        const int w = src.getWidth() / 2, h = src.getHeight() / 2;
        juce::Image dst (juce::Image::ARGB, w, h, false);
        const juce::Image::BitmapData in (src, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData out (dst, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto a = in.getPixelColour (2 * x, 2 * y), b = in.getPixelColour (2 * x + 1, 2 * y);
                const auto c = in.getPixelColour (2 * x, 2 * y + 1), d = in.getPixelColour (2 * x + 1, 2 * y + 1);
                auto avg = [] (int p, int q, int r, int s) { return (juce::uint8) ((p + q + r + s + 2) / 4); };
                out.setPixelColour (x, y, juce::Colour (avg (a.getRed(), b.getRed(), c.getRed(), d.getRed()),
                                                        avg (a.getGreen(), b.getGreen(), c.getGreen(), d.getGreen()),
                                                        avg (a.getBlue(), b.getBlue(), c.getBlue(), d.getBlue()),
                                                        avg (a.getAlpha(), b.getAlpha(), c.getAlpha(), d.getAlpha())));
            }
        return dst;
    }
}

FaceplateKit& FaceplateKit::get()
{
    static FaceplateKit kit;
    return kit;
}

FaceplateKit::FaceplateKit()
{
    auto full = juce::ImageCache::getFromMemory (PlaygroundBinary::MainFaceplate_png, PlaygroundBinary::MainFaceplate_pngSize);
    if (! full.isValid()) return;
    mips[0] = full.convertedToFormat (juce::Image::ARGB);
    mips[1] = halve (mips[0]);
    mips[2] = halve (mips[1]);
}

const juce::Image& FaceplateKit::mipFor (float scale, float& mipScale) const
{
    // the smallest copy that is still at least as big as what we draw
    if (scale <= 0.25f) { mipScale = 0.25f; return mips[2]; }
    if (scale <= 0.5f)  { mipScale = 0.5f;  return mips[1]; }
    mipScale = 1.0f;
    return mips[0];
}

void FaceplateKit::drawRegion (juce::Graphics& g, const juce::Image& img, float ms, juce::Rectangle<float> s, juce::Rectangle<float> d) const
{
    if (s.isEmpty() || d.isEmpty()) return;
    const auto si = (s * ms).toNearestInt();
    if (si.isEmpty()) return;
    g.drawImage (img, juce::roundToInt (d.getX()), juce::roundToInt (d.getY()),
                 juce::jmax (1, juce::roundToInt (d.getRight()) - juce::roundToInt (d.getX())),
                 juce::jmax (1, juce::roundToInt (d.getBottom()) - juce::roundToInt (d.getY())),
                 si.getX(), si.getY(), si.getWidth(), si.getHeight());
}

void FaceplateKit::draw (juce::Graphics& g, const Piece& p, juce::Rectangle<float> dest, float scale) const
{
    if (! isLoaded() || dest.isEmpty()) return;
    // pick the mip from the larger of the two axis scales so nothing is upscaled from a small copy
    const float sx = dest.getWidth() / (float) p.src.getWidth(), sy = dest.getHeight() / (float) p.src.getHeight();
    float ms = 1.0f;
    const auto& img = mipFor (juce::jmax (scale, sx, sy), ms);
    juce::Graphics::ScopedSaveState save (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (p.clipRadius > 0.0f)
    {
        juce::Path clip;
        clip.addRoundedRectangle (dest.reduced (1.0f), juce::jmin (p.clipRadius * scale, dest.getHeight() * 0.5f));
        g.reduceClipRegion (clip);
    }

    const auto s = p.src.toFloat();
    // insets in the destination, never more than half the destination size
    const float dl = juce::jmin ((float) p.insets.getLeft() * scale, dest.getWidth() * 0.5f);
    const float dr = juce::jmin ((float) p.insets.getRight() * scale, dest.getWidth() * 0.5f);
    const float dt = juce::jmin ((float) p.insets.getTop() * scale, dest.getHeight() * 0.5f);
    const float db = juce::jmin ((float) p.insets.getBottom() * scale, dest.getHeight() * 0.5f);
    const float sl = (float) p.insets.getLeft(), sr = (float) p.insets.getRight();
    const float st = (float) p.insets.getTop(), sb = (float) p.insets.getBottom();

    const float sxs[4] { s.getX(), s.getX() + sl, s.getRight() - sr, s.getRight() };
    const float sys[4] { s.getY(), s.getY() + st, s.getBottom() - sb, s.getBottom() };
    const float dxs[4] { dest.getX(), dest.getX() + dl, dest.getRight() - dr, dest.getRight() };
    const float dys[4] { dest.getY(), dest.getY() + dt, dest.getBottom() - db, dest.getBottom() };

    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            drawRegion (g, img, ms, { sxs[col], sys[row], sxs[col + 1] - sxs[col], sys[row + 1] - sys[row] },
                                    { dxs[col], dys[row], dxs[col + 1] - dxs[col], dys[row + 1] - dys[row] });
}

void FaceplateKit::drawSocket (juce::Graphics& g, juce::Rectangle<float> dest) const
{
    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addEllipse (dest.reduced (dest.getWidth() * 0.025f));
    g.reduceClipRegion (clip);
    draw (g, socket, dest, dest.getWidth() / (float) socket.src.getWidth());
}

void FaceplateKit::drawFrameBorder (juce::Graphics& g, juce::Rectangle<float> dest, float scale) const
{
    if (! isLoaded()) return;
    float ms = 1.0f;
    const auto& img = mipFor (scale, ms);
    juce::Graphics::ScopedSaveState save (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto& p = frame;
    const auto s = p.src.toFloat();
    const float sl = (float) p.insets.getLeft(), sr = (float) p.insets.getRight(), st = (float) p.insets.getTop(), sb = (float) p.insets.getBottom();
    const float dl = sl * scale, dr = sr * scale, dt = st * scale, db = sb * scale;
    const float sxs[4] { s.getX(), s.getX() + sl, s.getRight() - sr, s.getRight() };
    const float sys[4] { s.getY(), s.getY() + st, s.getBottom() - sb, s.getBottom() };
    const float dxs[4] { dest.getX(), dest.getX() + dl, dest.getRight() - dr, dest.getRight() };
    const float dys[4] { dest.getY(), dest.getY() + dt, dest.getBottom() - db, dest.getBottom() };
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            if (! (row == 1 && col == 1))
                drawRegion (g, img, ms, { sxs[col], sys[row], sxs[col + 1] - sxs[col], sys[row + 1] - sys[row] },
                                        { dxs[col], dys[row], dxs[col + 1] - dxs[col], dys[row + 1] - dys[row] });
}
} // namespace pg::ui
