#include "pch.h"

#include "include/ZePlotView.h"
#include "include/ZeTheme.h"
#include "ZeFrameCollector.h"

#include <utility>

namespace Kit {

    PlotView::PlotView() {
        ApplyTheme(Themes::Light());

        SetSize(320.0f, 200.0f);
    }

    const BoxStyle& PlotView::themeBox(const Theme& t) const { return t.plot; }

    void PlotView::SetVertices(std::vector<PlotVertex> verts) {
        verts_ = std::move(verts);
        RequestWindowRedraw();
    }

    void PlotView::SetTexts(std::vector<PlotText> texts) {
        texts_ = std::move(texts);
        RequestWindowRedraw();
    }

    void PlotView::Clear() {
        verts_.clear();
        texts_.clear();
    }

    void PlotView::contentOrigin(float& cx, float& cy) const {
        cx = x() + box_.border.width[Border::Left] + box_.padding.left;
        cy = y() + box_.border.width[Border::Top] + box_.padding.top;
    }

    void PlotView::contentSize(float& w, float& h) const {
        const float cw = width() - box_.border.width[Border::Left]
                                - box_.border.width[Border::Right]
                                - box_.padding.left - box_.padding.right;
        const float ch = height() - box_.border.width[Border::Top]
                                 - box_.border.width[Border::Bottom]
                                 - box_.padding.top - box_.padding.bottom;
        w = (cw > 0.0f) ? cw : 0.0f;
        h = (ch > 0.0f) ? ch : 0.0f;
    }

    void PlotView::toLocal(float winX, float winY, float& lx, float& ly) const {
        float cx, cy;
        contentOrigin(cx, cy);
        lx = winX - cx;
        ly = winY - cy;
    }

    void PlotView::paintContent(FrameCollector& fc) const {
        float cx, cy, cw, ch;
        contentOrigin(cx, cy);
        contentSize(cw, ch);
        if (cw <= 0.0f || ch <= 0.0f) return;

        if (!verts_.empty())
            fc.AddTriangles(verts_.data(), verts_.size(), cx, cy);

        for (const PlotText& t : texts_) {
            if (t.text.empty()) continue;

            FontDesc font;
            font.pixelSize = t.pixelSize;
            font.SetBold(t.bold);

            const TextMetrics tm = fc.MeasureText(t.text, font);

            float tx = cx + t.x;
            if (t.align == PlotTextAlign::Center)      tx -= tm.width * 0.5f;
            else if (t.align == PlotTextAlign::Right)  tx -= tm.width;

            fc.AddText(t.text, font, tx, cy + t.y + tm.ascent, t.color);
        }
    }

    void PlotView::SetOnMousePress(PlotMouseAction handler)   { onPress_   = std::move(handler); }
    void PlotView::SetOnMouseMove(PlotMouseAction handler)    { onMove_    = std::move(handler); }
    void PlotView::SetOnMouseRelease(PlotMouseAction handler) { onRelease_ = std::move(handler); }
    void PlotView::SetOnMouseWheel(PlotMouseAction handler)   { onWheel_   = std::move(handler); }

    bool PlotView::OnMousePress(MouseEvent& e) {
        if (!onPress_) return false;

        float lx, ly;
        toLocal(e.x, e.y, lx, ly);

        lastX_ = lx; lastY_ = ly;
        lastValid_ = true;
        held_ = true;

        PlotMouseEvent pe;
        pe.x = lx;
        pe.y = ly;
        pe.button = e.button;
        pe.modifiers = e.modifiers;
        onPress_(pe);

        e.accept();
        return true;
    }

    bool PlotView::OnMouseMove(MouseEvent& e) {
        if (!onMove_) return false;

        float lx, ly;
        toLocal(e.x, e.y, lx, ly);

        const float dx = lastValid_ ? (lx - lastX_) : 0.0f;
        const float dy = lastValid_ ? (ly - lastY_) : 0.0f;
        lastX_ = lx; lastY_ = ly;
        lastValid_ = true;

        PlotMouseEvent me;
        me.x = lx;
        me.y = ly;
        me.dx = dx;
        me.dy = dy;
        me.button = e.button;
        me.modifiers = e.modifiers;
        onMove_(me);

        if (!held_) return false;

        e.accept();
        return true;
    }

    bool PlotView::OnMouseRelease(MouseEvent& e) {
        held_ = false;

        if (!onRelease_) return false;

        float lx, ly;
        toLocal(e.x, e.y, lx, ly);

        const float dx = lastValid_ ? (lx - lastX_) : 0.0f;
        const float dy = lastValid_ ? (ly - lastY_) : 0.0f;
        lastX_ = lx; lastY_ = ly;
        lastValid_ = true;

        PlotMouseEvent re;
        re.x = lx;
        re.y = ly;
        re.dx = dx;
        re.dy = dy;
        re.button = e.button;
        re.modifiers = e.modifiers;
        onRelease_(re);

        e.accept();
        return true;
    }

    bool PlotView::OnMouseWheel(MouseEvent& e) {
        if (!onWheel_) return false;

        float lx, ly;
        toLocal(e.x, e.y, lx, ly);

        PlotMouseEvent we;
        we.x = lx;
        we.y = ly;
        we.wheelDelta = e.wheelDelta;
        we.button = e.button;
        we.modifiers = e.modifiers;
        onWheel_(we);

        e.accept();
        return true;
    }

}
