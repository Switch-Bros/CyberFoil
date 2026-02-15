#pragma once

#include <pu/Plutonium>
#include <string>

namespace inst::ui {

// Reusable single-line text presenter with:
// - clipped idle state + right fade hint when overflowing
// - selected-only marquee scrolling (pause/scroll/fade/reset loop)
// Intended to be drop-in for UI labels that can overflow localized text.
class OverflowText {
    public:
        OverflowText(int fontSize, pu::ui::Color textColor);
        PU_SMART_CTOR(OverflowText)

        // Adds internal render elements to the target layout once.
        void Attach(pu::ui::Layout* layout);
        // Pixel bounds of the visible text area.
        void SetBounds(int x, int y, int width, int height);
        void SetBackgroundColor(pu::ui::Color color);
        // Full logical text (component handles normalization/clipping).
        void SetText(const std::string& text);
        // Marquee only runs while selected and overflowing.
        void SetSelected(bool selected, bool forceReset = false);
        void SetVisible(bool visible);
        // Call periodically to animate marquee and refresh visual state.
        void Update(bool forceReset = false);
        bool IsOverflowing() const;

    private:
        pu::ui::elm::TextBlock::Ref baseText;
        pu::ui::elm::TextBlock::Ref probeText;
        pu::ui::elm::TextBlock::Ref marqueeText;
        pu::ui::elm::Element::Ref clipBegin;
        pu::ui::elm::Element::Ref clipEnd;
        pu::ui::elm::Rectangle::Ref fadeHint0;
        pu::ui::elm::Rectangle::Ref fadeHint1;
        pu::ui::elm::Rectangle::Ref fadeHint2;
        pu::ui::elm::Rectangle::Ref marqueeFadeRect;

        std::string fullText;
        std::string clippedText;
        pu::ui::Color textColor;
        pu::ui::Color backgroundColor;
        int fontSize = 22;
        int singleLineHeight = 1;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool attached = false;
        bool visible = true;
        bool selected = false;
        bool overflowing = false;
        bool clipEnabled = false;
        int clipX = 0;
        int clipY = 0;
        int clipW = 0;
        int clipH = 0;
        int marqueeOffset = 0;
        int marqueeMaxOffset = 0;
        u64 marqueeLastTick = 0;
        u64 marqueePauseUntilTick = 0;
        u64 marqueeEndPauseUntilTick = 0;
        u64 marqueeSpeedRemainder = 0;
        u64 marqueeFadeStartTick = 0;
        int marqueePhase = 0;
        int marqueeFadeAlpha = 0;

        std::string normalizeSingleLine(const std::string& text) const;
        std::string buildClippedText(const std::string& text, bool& overflow) const;
        void updateStaticLabel();
        void updateFadeHintRects();
        void hideMarquee(bool resetState);
};

}
