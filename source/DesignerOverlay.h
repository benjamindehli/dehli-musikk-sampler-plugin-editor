#pragma once

// GUI-designer overlay (milestone 4). Sits exactly over the hosted editor's
// manifest face and, while design mode is on, turns mouse gestures into rect
// edits on the MODEL: click to select (synced to the inspector), drag to move,
// drag a corner/edge handle to resize, arrow keys to nudge (shift = 10). Edits
// snap to other elements' edges/centres and the face borders, with alignment
// guides drawn live. A gesture commits ONCE on mouse-up through the UI-only hot
// path (no sample decode).
//
// All editing happens in DESIGN coordinates (the manifest's uncropped space);
// the overlay maps to face pixels via scale + cropTop. The face component is
// re-located on a timer — mode switches and hot reloads replace it.

#include "ModelTree.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace dmse_studio
{

class DesignerOverlay : public juce::Component, private juce::Timer
{
public:
    std::function<dm::Mode*()> getMode;                       // MODEL mode being designed
    std::function<int()> getModeIndex;                        // its index (for NodeRefs)
    std::function<juce::Component*()> findFace;               // live face inside the hosted editor
    std::function<void (NodeRef)> onSelect;                   // → inspector
    std::function<void (NodeRef, dm::Rect)> onCommitRect;     // undo snapshot + model + hot reload

    DesignerOverlay()
    {
        setWantsKeyboardFocus (true);
        startTimerHz (10);   // track the face across hot reloads / mode switches
    }

    void setSelection (NodeRef r) { selection = r; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto* m = getMode ? getMode() : nullptr;
        if (m == nullptr)
            return;

        g.setColour (juce::Colour (0xff6a9bd1).withAlpha (0.9f));
        g.drawRect (getLocalBounds(), 1);   // design-mode indicator

        // Faint outlines make every editable element discoverable.
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        for (const auto& el : elements (*m))
            g.drawRect (toOverlay (el.rect), 1.0f);

        // Selection + handles (or the drag ghost).
        const auto sel = dragging ? ghost : selectedRect (*m);
        if (sel.has_value())
        {
            const auto r = toOverlay (*sel);
            g.setColour (juce::Colour (0xff6a9bd1));
            g.drawRect (r, 2.0f);
            g.setColour (juce::Colours::white);
            for (const auto& h : handleCentres (r))
                g.fillRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre (h));
        }

        // Snap guides.
        g.setColour (juce::Colour (0xffe0b34a));
        for (auto gx : guideXs) g.drawVerticalLine   (juce::roundToInt (gx), 0.0f, (float) getHeight());
        for (auto gy : guideYs) g.drawHorizontalLine (juce::roundToInt (gy), 0.0f, (float) getWidth());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        guideXs.clear(); guideYs.clear();
        auto* m = getMode ? getMode() : nullptr;
        if (m == nullptr)
            return;

        grabKeyboardFocus();
        const auto pos = toDesign (e.position);

        // A handle of the current selection wins over re-selection.
        if (auto sel = selectedRect (*m))
        {
            dragHandle = handleAt (toOverlay (*sel), e.position);
            if (dragHandle >= 0)
            {
                dragging = true;
                startRect = ghost = *sel;
                dragStart = pos;
                return;
            }
        }

        // Topmost element under the mouse (interactive kinds win over images).
        NodeRef hit;
        const auto els = elements (*m);
        for (int i = els.size(); --i >= 0;)
            if (contains (els.getReference (i).rect, pos))
            {
                hit = els.getReference (i).ref;
                break;
            }

        selection = hit;
        if (onSelect)
            onSelect (hit);
        repaint();

        if (auto sel = selectedRect (*m))
        {
            dragging  = true;
            dragHandle = -1;   // move
            startRect = ghost = *sel;
            dragStart = pos;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* m = getMode ? getMode() : nullptr;
        if (! dragging || m == nullptr)
            return;

        const auto pos = toDesign (e.position);
        const int dx = juce::roundToInt (pos.x - dragStart.x);
        const int dy = juce::roundToInt (pos.y - dragStart.y);

        auto r = startRect;
        if (dragHandle < 0)
        {
            r.x += dx;
            r.y += dy;
        }
        else
        {
            // Handles: 0..7 = corners + edge midpoints (TL TR BL BR L R T B).
            const bool left   = dragHandle == 0 || dragHandle == 2 || dragHandle == 4;
            const bool right  = dragHandle == 1 || dragHandle == 3 || dragHandle == 5;
            const bool top    = dragHandle == 0 || dragHandle == 1 || dragHandle == 6;
            const bool bottom = dragHandle == 2 || dragHandle == 3 || dragHandle == 7;
            if (left)   { r.x += dx; r.width  -= dx; }
            if (right)  { r.width  += dx; }
            if (top)    { r.y += dy; r.height -= dy; }
            if (bottom) { r.height += dy; }
            r.width  = juce::jmax (2, r.width);
            r.height = juce::jmax (2, r.height);
        }

        snap (*m, r);
        ghost = r;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        guideXs.clear(); guideYs.clear();
        if (! dragging)
            return;
        dragging = false;
        if (! (ghost.x == startRect.x && ghost.y == startRect.y
               && ghost.width == startRect.width && ghost.height == startRect.height))
            if (onCommitRect && selection.kind != NodeRef::Kind::none)
                onCommitRect (selection, ghost);
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        auto* m = getMode ? getMode() : nullptr;
        if (m == nullptr)
            return false;
        auto sel = selectedRect (*m);
        if (! sel.has_value())
            return false;

        const int step = key.getModifiers().isShiftDown() ? 10 : 1;
        auto r = *sel;
        if      (key == juce::KeyPress::leftKey)  r.x -= step;
        else if (key == juce::KeyPress::rightKey) r.x += step;
        else if (key == juce::KeyPress::upKey)    r.y -= step;
        else if (key == juce::KeyPress::downKey)  r.y += step;
        else return false;

        if (onCommitRect)
            onCommitRect (selection, r);
        return true;
    }

private:
    struct Element { NodeRef ref; dm::Rect rect; };

    // Hit/snap list in priority order: images lowest, then menus, buttons, knobs.
    juce::Array<Element> elements (dm::Mode& m) const
    {
        juce::Array<Element> out;
        if (m.ui.tabs.isEmpty())
            return out;
        auto& tab = m.ui.tabs.getReference (0);
        const int mi = getModeIndex ? getModeIndex() : 0;
        for (int i = 0; i < tab.images.size(); ++i)
            out.add ({ { NodeRef::Kind::image, mi, i },  tab.images.getReference (i).rect });
        for (int i = 0; i < tab.menus.size(); ++i)
            out.add ({ { NodeRef::Kind::menu, mi, i },   tab.menus.getReference (i).rect });
        for (int i = 0; i < tab.buttons.size(); ++i)
            out.add ({ { NodeRef::Kind::button, mi, i }, tab.buttons.getReference (i).rect });
        for (int i = 0; i < tab.controls.size(); ++i)
            out.add ({ { NodeRef::Kind::control, mi, i }, tab.controls.getReference (i).rect });
        return out;
    }

    std::optional<dm::Rect> selectedRect (dm::Mode& m) const
    {
        if (m.ui.tabs.isEmpty())
            return std::nullopt;
        auto& tab = m.ui.tabs.getReference (0);
        if (selection.kind == NodeRef::Kind::control && selection.a >= 0 && selection.a < tab.controls.size())
            return tab.controls.getReference (selection.a).rect;
        if (selection.kind == NodeRef::Kind::button && selection.a >= 0 && selection.a < tab.buttons.size())
            return tab.buttons.getReference (selection.a).rect;
        if (selection.kind == NodeRef::Kind::menu && selection.a >= 0 && selection.a < tab.menus.size())
            return tab.menus.getReference (selection.a).rect;
        if (selection.kind == NodeRef::Kind::image && selection.a >= 0 && selection.a < tab.images.size())
            return tab.images.getReference (selection.a).rect;
        return std::nullopt;
    }

    // ── coordinate mapping (design space ⇄ overlay pixels) ──────────────────
    void updateMapping (dm::Mode& m)
    {
        uiW = juce::jmax (1, m.ui.width);
        crop = juce::jlimit (0, juce::jmax (0, m.ui.height - 1), m.ui.cropTop);
        uiH = juce::jmax (1, m.ui.height - crop);
        sx = (double) getWidth()  / uiW;
        sy = (double) getHeight() / uiH;
    }

    juce::Rectangle<float> toOverlay (const dm::Rect& r) const
    {
        return { (float) (r.x * sx), (float) ((r.y - crop) * sy),
                 (float) (r.width * sx), (float) (r.height * sy) };
    }

    juce::Point<float> toDesign (juce::Point<float> p) const
    {
        return { (float) (p.x / juce::jmax (1e-9, sx)),
                 (float) (p.y / juce::jmax (1e-9, sy)) + (float) crop };
    }

    static bool contains (const dm::Rect& r, juce::Point<float> p)
    {
        return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height;
    }

    // ── handles ──────────────────────────────────────────────────────────────
    static juce::Array<juce::Point<float>> handleCentres (juce::Rectangle<float> r)
    {
        return { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight(),
                 { r.getX(), r.getCentreY() }, { r.getRight(), r.getCentreY() },
                 { r.getCentreX(), r.getY() }, { r.getCentreX(), r.getBottom() } };
    }

    static int handleAt (juce::Rectangle<float> r, juce::Point<float> p)
    {
        const auto centres = handleCentres (r);
        for (int i = 0; i < centres.size(); ++i)
            if (p.getDistanceFrom (centres.getReference (i)) < 7.0f)
                return i;
        return -1;
    }

    // ── snapping (design coords, threshold in design px) ────────────────────
    void snap (dm::Mode& m, dm::Rect& r)
    {
        guideXs.clear(); guideYs.clear();
        constexpr int threshold = 5;

        juce::Array<int> xs { 0, uiW }, ys { crop, crop + uiH };
        for (const auto& el : elements (m))
        {
            if (el.ref == selection)
                continue;
            xs.add (el.rect.x); xs.add (el.rect.x + el.rect.width);  xs.add (el.rect.x + el.rect.width / 2);
            ys.add (el.rect.y); ys.add (el.rect.y + el.rect.height); ys.add (el.rect.y + el.rect.height / 2);
        }

        auto snapAxis = [threshold] (int candidateEdges[3], const juce::Array<int>& targets,
                                     int& coord, juce::Array<float>& guides,
                                     std::function<float (int)> toGuide)
        {
            for (int e = 0; e < 3; ++e)
                for (int t : targets)
                    if (std::abs (candidateEdges[e] - t) <= threshold)
                    {
                        coord += t - candidateEdges[e];
                        guides.add (toGuide (t));
                        return;
                    }
        };

        int ex[3] = { r.x, r.x + r.width, r.x + r.width / 2 };
        snapAxis (ex, xs, r.x, guideXs, [this] (int t) { return (float) (t * sx); });
        int ey[3] = { r.y, r.y + r.height, r.y + r.height / 2 };
        snapAxis (ey, ys, r.y, guideYs, [this] (int t) { return (float) ((t - crop) * sy); });
    }

    void timerCallback() override
    {
        // Track the live face: hot reloads and mode switches replace the component.
        auto* face = findFace ? findFace() : nullptr;
        auto* m = getMode ? getMode() : nullptr;
        if (face == nullptr || m == nullptr || getParentComponent() == nullptr)
            return;
        const auto target = getParentComponent()->getLocalArea (face, face->getLocalBounds());
        if (target != getBounds())
            setBounds (target);
        updateMapping (*m);
    }

    NodeRef selection;
    bool dragging = false;
    int dragHandle = -1;
    dm::Rect startRect, ghost;
    juce::Point<float> dragStart;
    juce::Array<float> guideXs, guideYs;

    int uiW = 1, uiH = 1, crop = 0;
    double sx = 1.0, sy = 1.0;
};

} // namespace dmse_studio
