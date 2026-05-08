#include "ElaraCodeEditorWidget.h"
#include "ElaraRootWidget.h"
#include <stdio.h>

namespace elara {

static const unsigned int ELARA_KEY_BACKSPACE = 0xff08;
static const unsigned int ELARA_KEY_RETURN    = 0xff0d;
static const unsigned int ELARA_KEY_HOME      = 0xff50;
static const unsigned int ELARA_KEY_LEFT      = 0xff51;
static const unsigned int ELARA_KEY_UP        = 0xff52;
static const unsigned int ELARA_KEY_RIGHT     = 0xff53;
static const unsigned int ELARA_KEY_DOWN      = 0xff54;
static const unsigned int ELARA_KEY_PAGE_UP   = 0xff55;
static const unsigned int ELARA_KEY_PAGE_DOWN = 0xff56;
static const unsigned int ELARA_KEY_END       = 0xff57;
static const unsigned int ELARA_KEY_DELETE    = 0xffff;
static const unsigned int ELARA_KEY_KP_ENTER  = 0xff8d;
static const unsigned int ELARA_KEY_TAB       = 0xff09;

static String intToStr(int n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", n);
    return String(buf);
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

ElaraCodeEditorWidget::ElaraCodeEditorWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraWidget(root_widget, widget_handle),
    vertical_slider(new ElaraSliderWidget(
        root_widget,
        ElaraWidgetHandle(
            String((const char*)widget_handle.getHandle().getPtr(),
                   widget_handle.getHandle().length()) + String(".vscroll")
        )
    )),
    horizontal_slider(new ElaraSliderWidget(
        root_widget,
        ElaraWidgetHandle(
            String((const char*)widget_handle.getHandle().getPtr(),
                   widget_handle.getHandle().length()) + String(".hscroll")
        )
    )),
    value(""),
    palette_master("input"),
    enabled(true),
    focused(false),
    font_size(14.0),
    line_height(20.0),
    scrollbar_size(18.0),
    gutter_width(60.0),
    minimap_width(100.0),
    padding_x(6.0),
    caret_index(0),
    preferred_column(-1),
    scroll_x(0),
    scroll_y(0),
    minimap_dragging(false) {

    vertical_slider->setOrientation("vertical");
    vertical_slider->setStep(1);
    vertical_slider->setZOrder(10);

    horizontal_slider->setOrientation("horizontal");
    horizontal_slider->setStep(1);
    horizontal_slider->setZOrder(10);

    addChild(Ref<ElaraWidget>(vertical_slider));
    addChild(Ref<ElaraWidget>(horizontal_slider));
}

ElaraCodeEditorWidget::~ElaraCodeEditorWidget() {}

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

double ElaraCodeEditorWidget::effectiveScrollbarW() const {
    return vertical_slider->isVisible() ? scrollbar_size : 0.0;
}

double ElaraCodeEditorWidget::effectiveScrollbarH() const {
    return horizontal_slider->isVisible() ? scrollbar_size : 0.0;
}

double ElaraCodeEditorWidget::editorLeft() const {
    return gutter_width;
}

double ElaraCodeEditorWidget::editorRight() const {
    return width - minimap_width - effectiveScrollbarW();
}

double ElaraCodeEditorWidget::editorContentWidth() const {
    double w = editorRight() - editorLeft();
    return w > 0 ? w : 0;
}

double ElaraCodeEditorWidget::minimapLeft() const {
    return width - minimap_width - effectiveScrollbarW();
}

double ElaraCodeEditorWidget::charWidth() const {
    return font_size * 0.58;
}

int ElaraCodeEditorWidget::viewportLineCount() const {
    double h = height - effectiveScrollbarH();
    int count = (int)(h / line_height);
    return count > 1 ? count : 1;
}

int ElaraCodeEditorWidget::viewportCharCount() const {
    double w = editorContentWidth() - padding_x * 2;
    int count = (int)(w / charWidth());
    return count > 1 ? count : 1;
}

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

int ElaraCodeEditorWidget::logicalLineCount() const {
    int count = 1;
    for (int i = 0; i < (int)value.length(); i++) {
        if (value.byteAt(i) == '\n') count++;
    }
    return count;
}

int ElaraCodeEditorWidget::logicalLineStart(int line) const {
    int current = 0;
    for (int i = 0; i < (int)value.length(); i++) {
        if (current == line) return i;
        if (value.byteAt(i) == '\n') current++;
    }
    return (int)value.length();
}

int ElaraCodeEditorWidget::logicalLineEnd(int line) const {
    int start = logicalLineStart(line);
    int end = start;
    while (end < (int)value.length() && value.byteAt(end) != '\n') {
        end++;
    }
    return end;
}

String ElaraCodeEditorWidget::logicalLineText(int line) const {
    int start = logicalLineStart(line);
    int end   = logicalLineEnd(line);
    return value.substr(start, end - start);
}

int ElaraCodeEditorWidget::logicalLineForIndex(int idx) const {
    int line = 0;
    for (int i = 0; i < idx && i < (int)value.length(); i++) {
        if (value.byteAt(i) == '\n') line++;
    }
    return line;
}

int ElaraCodeEditorWidget::columnForIndex(int idx) const {
    int line  = logicalLineForIndex(idx);
    int start = logicalLineStart(line);
    return idx - start;
}

int ElaraCodeEditorWidget::indexForLineColumn(int line, int col) const {
    int start = logicalLineStart(line);
    int end   = logicalLineEnd(line);
    int len   = end - start;
    if (col < 0)   col = 0;
    if (col > len) col = len;
    return start + col;
}

int ElaraCodeEditorWidget::longestVisibleLineLength() const {
    int longest = 0;
    int n = (int)visible_line_map.length();
    for (int i = 0; i < n; i++) {
        int len = (int)logicalLineText(visible_line_map[i]).length();
        if (len > longest) longest = len;
    }
    return longest;
}

// ---------------------------------------------------------------------------
// Fold management
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::rebuildFolds() {
    folds.clear();

    int total = logicalLineCount();
    int stack[512];
    int stack_depth = 0;

    for (int i = 0; i < total; i++) {
        String line = logicalLineText(i);
        int len = (int)line.length();

        // Find last non-whitespace byte
        int last = -1;
        for (int j = len - 1; j >= 0; j--) {
            char ch = line.byteAt(j);
            if (ch != ' ' && ch != '\t' && ch != '\r') { last = j; break; }
        }
        if (last >= 0 && line.byteAt(last) == '{') {
            if (stack_depth < 512) stack[stack_depth++] = i;
        }

        // Find first non-whitespace byte
        int first = -1;
        for (int j = 0; j < len; j++) {
            char ch = line.byteAt(j);
            if (ch != ' ' && ch != '\t' && ch != '\r') { first = j; break; }
        }
        if (first >= 0 && line.byteAt(first) == '}' && stack_depth > 0) {
            int start = stack[--stack_depth];
            if (i > start + 1) {
                folds.push(FoldRegion(start, i));
            }
        }
    }
}

void ElaraCodeEditorWidget::rebuildVisibleLineMap() {
    visible_line_map.clear();
    int total = logicalLineCount();
    for (int i = 0; i < total; i++) {
        if (!isLineFoldedAway(i)) {
            visible_line_map.push(i);
        }
    }
}

bool ElaraCodeEditorWidget::isLineFoldedAway(int logical_line) const {
    for (int i = 0; i < (int)folds.length(); i++) {
        if (folds[i].collapsed &&
            logical_line > folds[i].start_line &&
            logical_line <= folds[i].end_line) {
            return true;
        }
    }
    return false;
}

ElaraCodeEditorWidget::FoldRegion*
ElaraCodeEditorWidget::foldStartingAt(int logical_line) {
    for (int i = 0; i < (int)folds.length(); i++) {
        if (folds[i].start_line == logical_line) return &folds[i];
    }
    return 0;
}

const ElaraCodeEditorWidget::FoldRegion*
ElaraCodeEditorWidget::foldStartingAt(int logical_line) const {
    for (int i = 0; i < (int)folds.length(); i++) {
        if (folds[i].start_line == logical_line) return &folds[i];
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Decoration helpers
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::ensureDecorations(int logical_line) {
    while ((int)decorations.length() <= logical_line) {
        decorations.push(LineDecoration());
    }
}

// ---------------------------------------------------------------------------
// Scroll helpers
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::clampCaret() {
    if (caret_index < 0) caret_index = 0;
    if (caret_index > (int)value.length()) caret_index = (int)value.length();
}

void ElaraCodeEditorWidget::clampScroll() {
    int vis = (int)visible_line_map.length();
    int max_y = vis - viewportLineCount();
    if (max_y < 0) max_y = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_y) scroll_y = max_y;

    int max_x = longestVisibleLineLength() - viewportCharCount();
    if (max_x < 0) max_x = 0;
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_x > max_x) scroll_x = max_x;
}

void ElaraCodeEditorWidget::updateScrollbars() {
    clampScroll();

    int vis   = (int)visible_line_map.length();
    int max_y = vis - viewportLineCount();
    if (max_y < 0) max_y = 0;
    int max_x = longestVisibleLineLength() - viewportCharCount();
    if (max_x < 0) max_x = 0;

    vertical_slider->setVisible(max_y > 0);
    horizontal_slider->setVisible(max_x > 0);

    // Second pass: hiding a scrollbar changes effective viewport, recompute ranges.
    max_y = vis - viewportLineCount();
    if (max_y < 0) max_y = 0;
    max_x = longestVisibleLineLength() - viewportCharCount();
    if (max_x < 0) max_x = 0;

    if (vertical_slider->isVisible()) {
        vertical_slider->setRange(0, max_y);
        vertical_slider->setStep(1);
        vertical_slider->setValue(scroll_y);
    }
    if (horizontal_slider->isVisible()) {
        horizontal_slider->setRange(0, max_x);
        horizontal_slider->setStep(1);
        horizontal_slider->setValue(scroll_x);
    }
}

void ElaraCodeEditorWidget::scrollToCaret() {
    int caret_logical = logicalLineForIndex(caret_index);
    int caret_col     = columnForIndex(caret_index);

    // Find visible index for caret logical line
    int vis_idx = -1;
    for (int i = 0; i < (int)visible_line_map.length(); i++) {
        if (visible_line_map[i] == caret_logical) { vis_idx = i; break; }
    }

    if (vis_idx >= 0) {
        if (vis_idx < scroll_y) {
            scroll_y = vis_idx;
        } else if (vis_idx >= scroll_y + viewportLineCount()) {
            scroll_y = vis_idx - viewportLineCount() + 1;
        }
    }

    if (caret_col < scroll_x) {
        scroll_x = caret_col;
    } else if (caret_col >= scroll_x + viewportCharCount()) {
        scroll_x = caret_col - viewportCharCount() + 1;
    }
}

// ---------------------------------------------------------------------------
// Viewport metrics
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::rebuildViewportMetrics(ElaraDrawContext* ctx) {
    viewport_metrics.clear();

    if (!ctx) return;

    int vis_total   = (int)visible_line_map.length();
    int vp_count    = viewportLineCount();

    for (int i = 0; i < vp_count; i++) {
        int vis_idx = scroll_y + i;
        if (vis_idx >= vis_total) break;

        int logical = visible_line_map[vis_idx];
        String line = logicalLineText(logical);

        // Apply horizontal scroll
        String visible_text;
        if (scroll_x < (int)line.length()) {
            visible_text = line.substr(scroll_x);
            int max_chars = viewportCharCount() + 2; // slight overcount to avoid clipping
            if ((int)visible_text.length() > max_chars) {
                visible_text = visible_text.substr(0, max_chars);
            }
        }

        VisibleLineMetrics m;
        m.logical_line  = logical;
        m.visible_text  = visible_text;
        m.caret_positions.push(0.0);

        for (int j = 1; j <= (int)visible_text.length(); j++) {
            String prefix = visible_text.substr(0, j);
            m.caret_positions.push(ctx->measureTextWidth(prefix, font_size));
        }

        viewport_metrics.push(m);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::drawGutter(ElaraDrawContext* ctx) {
    double gh = height - effectiveScrollbarH();

    ElaraPaletteTriplet gc = colors(palette_master, "gutter");
    // Gutter background — slightly different: darken base a touch
    double gb_r = gc.base.r * 0.92;
    double gb_g = gc.base.g * 0.92;
    double gb_b = gc.base.b * 0.92;
    ctx->setColor(gb_r, gb_g, gb_b);
    ctx->fillRect(0, 0, gutter_width, gh);

    // Right-edge border
    ctx->setColor(gc.accent.r, gc.accent.g, gc.accent.b);
    ctx->line(gutter_width - 1, 0, gutter_width - 1, gh, 1);

    int vis_total = (int)visible_line_map.length();
    int vp_count  = viewportLineCount();

    for (int i = 0; i < vp_count; i++) {
        int vis_idx = scroll_y + i;
        if (vis_idx >= vis_total) break;

        int logical  = visible_line_map[vis_idx];
        double row_y = (double)i * line_height;
        double row_cy = row_y + line_height * 0.5;

        // --- Breakpoint dot (left 12px zone) ---
        if (logical < (int)decorations.length() && decorations[logical].breakpoint) {
            ctx->setColor(0.86, 0.27, 0.22);
            double r = line_height * 0.32;
            ctx->fillCircle(8.0, row_cy, r);
        }

        // --- Bookmark bar (12..20px zone) ---
        if (logical < (int)decorations.length() && decorations[logical].bookmark) {
            ctx->setColor(0.28, 0.58, 0.88);
            ctx->fillRect(14.0, row_y + 3.0, 4.0, line_height - 6.0);
        }

        // --- Line number (right-aligned before fold indicator) ---
        // Dim text for non-caret lines, full text for caret line
        int caret_logical = logicalLineForIndex(caret_index);
        if (logical == caret_logical) {
            ctx->setColor(gc.text.r, gc.text.g, gc.text.b);
        } else {
            ctx->setColor(gc.text.r * 0.55, gc.text.g * 0.55, gc.text.b * 0.55);
        }

        String num = intToStr(logical + 1);
        double num_w = ctx->measureTextWidth(num, font_size - 2.0);
        double num_x = gutter_width - 18.0 - num_w;
        if (num_x < 20.0) num_x = 20.0;
        ctx->drawText(num_x, row_y + font_size - 1.0, num, font_size - 2.0);

        // --- Fold indicator (rightmost 16px of gutter) ---
        const FoldRegion* fold = foldStartingAt(logical);
        if (fold) {
            ctx->setColor(gc.text.r * 0.70, gc.text.g * 0.70, gc.text.b * 0.70);
            double fx = gutter_width - 13.0;
            double fy = row_cy;

            if (fold->collapsed) {
                // ▶ right-pointing triangle
                ctx->line(fx,       fy - 5.0, fx + 8.0, fy,       1.0);
                ctx->line(fx + 8.0, fy,       fx,       fy + 5.0, 1.0);
                ctx->line(fx,       fy + 5.0, fx,       fy - 5.0, 1.0);
            } else {
                // ▼ down-pointing triangle
                ctx->line(fx,       fy - 3.0, fx + 8.0, fy - 3.0, 1.0);
                ctx->line(fx + 8.0, fy - 3.0, fx + 4.0, fy + 4.0, 1.0);
                ctx->line(fx + 4.0, fy + 4.0, fx,       fy - 3.0, 1.0);
            }
        }
    }
}

void ElaraCodeEditorWidget::drawEditor(ElaraDrawContext* ctx) {
    double ex = editorLeft();
    double ew = editorContentWidth();
    double eh = height - effectiveScrollbarH();

    String sub = enabled ? String("default") : String("disabled");
    ElaraPaletteTriplet c = colors(palette_master, sub);

    // Background
    ctx->setColor(c.base.r, c.base.g, c.base.b);
    ctx->fillRect(ex, 0, ew, eh);

    // Current line highlight
    int caret_logical = logicalLineForIndex(caret_index);
    if (focused) {
        for (int i = 0; i < (int)viewport_metrics.length(); i++) {
            if (viewport_metrics[i].logical_line == caret_logical) {
                double hy = (double)i * line_height;
                // Slightly lighter than base
                ctx->setColor(
                    c.base.r + (1.0 - c.base.r) * 0.08,
                    c.base.g + (1.0 - c.base.g) * 0.08,
                    c.base.b + (1.0 - c.base.b) * 0.08
                );
                ctx->fillRect(ex, hy, ew, line_height);
                break;
            }
        }
    }

    // Text lines
    ctx->setColor(c.text.r, c.text.g, c.text.b);
    for (int i = 0; i < (int)viewport_metrics.length(); i++) {
        const VisibleLineMetrics& m = viewport_metrics[i];
        double ty = (double)i * line_height + font_size;

        String display = m.visible_text;

        // Append fold hint if this is the start of a collapsed region
        const FoldRegion* fold = foldStartingAt(m.logical_line);
        if (fold && fold->collapsed) {
            display = display + String("  {…}");
        }

        if (display.length() > 0) {
            ctx->drawText(ex + padding_x, ty, display, font_size);
        }
    }

    // Caret
    if (focused) {
        int caret_col = columnForIndex(caret_index);

        for (int i = 0; i < (int)viewport_metrics.length(); i++) {
            if (viewport_metrics[i].logical_line != caret_logical) continue;

            const VisibleLineMetrics& m = viewport_metrics[i];
            int vis_col = caret_col - scroll_x;

            double cx = 0.0;
            if (vis_col > 0) {
                int idx = vis_col < (int)m.caret_positions.length()
                    ? vis_col
                    : (int)m.caret_positions.length() - 1;
                if (idx >= 0) cx = m.caret_positions[idx];
            }

            double cy = (double)i * line_height;
            ctx->setColor(c.text.r, c.text.g, c.text.b);
            ctx->line(ex + padding_x + cx, cy + 2.0,
                      ex + padding_x + cx, cy + line_height - 2.0, 1.5);
            break;
        }
    }

    // Right border between editor and minimap
    ctx->setColor(c.accent.r, c.accent.g, c.accent.b);
    ctx->line(ex + ew, 0, ex + ew, eh, 1.0);
}

void ElaraCodeEditorWidget::drawMinimap(ElaraDrawContext* ctx) {
    double mx = minimapLeft();
    double mh = height;

    ElaraPaletteTriplet mc = colors(palette_master, "default");

    // Minimap background (slightly darker than editor)
    ctx->setColor(mc.base.r * 0.88, mc.base.g * 0.88, mc.base.b * 0.88);
    ctx->fillRect(mx, 0, minimap_width, mh);

    int vis_total = (int)visible_line_map.length();
    if (vis_total == 0) return;

    // Pixels per line — compress if document is large
    double lph = mh / (double)vis_total;
    if (lph > 3.0) lph = 3.0;
    if (lph < 0.5) lph = 0.5;

    // Viewport indicator rectangle
    double vp_y0 = (double)scroll_y * lph;
    double vp_y1 = (double)(scroll_y + viewportLineCount()) * lph;
    ctx->setColor(mc.accent.r * 0.5 + 0.3, mc.accent.g * 0.5 + 0.3, mc.accent.b * 0.5 + 0.3);
    ctx->fillRect(mx, vp_y0, minimap_width, vp_y1 - vp_y0);

    // Render each visible line
    double cpw = minimap_width / 80.0; // pixels per character assuming 80-col wrap
    if (cpw < 0.5) cpw = 0.5;

    for (int i = 0; i < vis_total; i++) {
        int logical = visible_line_map[i];
        String line = logicalLineText(logical);
        double ly   = (double)i * lph;

        int chars = (int)line.length();
        if (chars > 80) chars = 80;

        // Breakpoint stripe on left edge of minimap
        if (logical < (int)decorations.length() && decorations[logical].breakpoint) {
            ctx->setColor(0.86, 0.27, 0.22);
            ctx->fillRect(mx, ly, 2.0, lph < 1.0 ? 1.0 : lph);
        }

        // Bookmark stripe
        if (logical < (int)decorations.length() && decorations[logical].bookmark) {
            ctx->setColor(0.28, 0.58, 0.88);
            ctx->fillRect(mx + 2.0, ly, 2.0, lph < 1.0 ? 1.0 : lph);
        }

        // Character dots
        for (int j = 0; j < chars; j++) {
            char ch = line.byteAt(j);
            if (ch <= ' ') continue;

            double dot_x = mx + 5.0 + (double)j * cpw;
            double dot_y = ly;
            double dot_w = cpw > 0.8 ? cpw - 0.3 : cpw;
            double dot_h = lph > 0.8 ? lph - 0.3 : lph;
            if (dot_w < 0.3) dot_w = 0.3;
            if (dot_h < 0.3) dot_h = 0.3;

            // Colour by character class — purely aesthetic
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
                ctx->setColor(mc.text.r * 0.7, mc.text.g * 0.7, mc.text.b * 0.7);
            } else if (ch >= '0' && ch <= '9') {
                ctx->setColor(0.55, 0.75, 0.55);
            } else if (ch == '"' || ch == '\'') {
                ctx->setColor(0.78, 0.62, 0.42);
            } else {
                ctx->setColor(mc.text.r * 0.45, mc.text.g * 0.45, mc.text.b * 0.45);
            }

            ctx->fillRect(dot_x, dot_y, dot_w, dot_h);
        }
    }

    // Left border of minimap
    ctx->setColor(mc.accent.r, mc.accent.g, mc.accent.b);
    ctx->line(mx, 0, mx, mh, 1.0);
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

int ElaraCodeEditorWidget::caretAtPoint(double px, double py) const {
    // px/py are relative to widget origin; px already corrected by caller to
    // be relative to the text content left edge (after gutter and padding).
    int vp_row = (int)(py / line_height);
    if (vp_row < 0) vp_row = 0;

    int vis_idx   = scroll_y + vp_row;
    int vis_total = (int)visible_line_map.length();
    if (vis_idx >= vis_total) vis_idx = vis_total > 0 ? vis_total - 1 : 0;
    if (vis_total == 0) return 0;

    int logical = visible_line_map[vis_idx];

    // Try cached metrics first
    for (int i = 0; i < (int)viewport_metrics.length(); i++) {
        const VisibleLineMetrics& m = viewport_metrics[i];
        if (m.logical_line != logical) continue;

        if (px <= 0.0 || (int)m.caret_positions.length() <= 1) {
            return indexForLineColumn(logical, scroll_x);
        }

        for (int j = 0; j < (int)m.visible_text.length(); j++) {
            double left  = m.caret_positions[j];
            double right = m.caret_positions[j + 1];
            double mid   = left + (right - left) * 0.5;
            if (px <= mid) {
                return indexForLineColumn(logical, scroll_x + j);
            }
        }
        return indexForLineColumn(logical, scroll_x + (int)m.visible_text.length());
    }

    // Fallback: use average char width
    int col = scroll_x + (int)(px / charWidth());
    if (col < 0) col = 0;
    return indexForLineColumn(logical, col);
}

int ElaraCodeEditorWidget::gutterLogicalLine(double py) const {
    int vp_row = (int)(py / line_height);
    if (vp_row < 0) return -1;

    int vis_idx   = scroll_y + vp_row;
    int vis_total = (int)visible_line_map.length();
    if (vis_idx >= vis_total) return -1;

    return visible_line_map[vis_idx];
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::insertText(const String& text) {
    String before = value.substr(0, caret_index);
    String after  = value.substr(caret_index);
    value = before + text + after;
    caret_index += (int)text.length();
    rebuildFolds();
    rebuildVisibleLineMap();
}

void ElaraCodeEditorWidget::backspace() {
    if (caret_index <= 0) return;
    String before = value.substr(0, caret_index - 1);
    String after  = value.substr(caret_index);
    value = before + after;
    caret_index--;
    rebuildFolds();
    rebuildVisibleLineMap();
}

void ElaraCodeEditorWidget::deleteForward() {
    if (caret_index >= (int)value.length()) return;
    String before = value.substr(0, caret_index);
    String after  = value.substr(caret_index + 1);
    value = before + after;
    rebuildFolds();
    rebuildVisibleLineMap();
}

void ElaraCodeEditorWidget::moveCaretHorizontal(int delta) {
    caret_index += delta;
    clampCaret();
    preferred_column = -1;
}

void ElaraCodeEditorWidget::moveCaretVertical(int delta) {
    int current_logical = logicalLineForIndex(caret_index);

    if (preferred_column < 0) {
        preferred_column = columnForIndex(caret_index);
    }

    // Find visible index of current line
    int vis_idx = -1;
    for (int i = 0; i < (int)visible_line_map.length(); i++) {
        if (visible_line_map[i] == current_logical) { vis_idx = i; break; }
    }

    // If caret is inside a collapsed fold, jump to the fold start
    if (vis_idx < 0) {
        for (int i = 0; i < (int)folds.length(); i++) {
            if (folds[i].collapsed &&
                current_logical > folds[i].start_line &&
                current_logical <= folds[i].end_line) {
                for (int j = 0; j < (int)visible_line_map.length(); j++) {
                    if (visible_line_map[j] == folds[i].start_line) {
                        vis_idx = j; break;
                    }
                }
                break;
            }
        }
    }

    if (vis_idx < 0) return;

    int target_vis = vis_idx + delta;
    if (target_vis < 0) target_vis = 0;
    if (target_vis >= (int)visible_line_map.length())
        target_vis = (int)visible_line_map.length() - 1;

    int target_logical = visible_line_map[target_vis];
    caret_index = indexForLineColumn(target_logical, preferred_column);
    clampCaret();
}

// ---------------------------------------------------------------------------
// Root widget helper
// ---------------------------------------------------------------------------

ElaraRootWidget* ElaraCodeEditorWidget::rootWidget() const {
    ElaraWidget* cur = (ElaraWidget*)this;
    while (cur) {
        ElaraRootWidget* root = dynamic_cast<ElaraRootWidget*>(cur);
        if (root) return root;
        cur = cur->getParent();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::setText(const String& text) {
    value = text;
    caret_index = 0;
    rebuildFolds();
    rebuildVisibleLineMap();
    clampCaret();
    updateScrollbars();
}

String ElaraCodeEditorWidget::getText() const {
    return value;
}

void ElaraCodeEditorWidget::setEnabled(bool v) {
    enabled = v;
    vertical_slider->setEnabled(v);
    horizontal_slider->setEnabled(v);
}

bool ElaraCodeEditorWidget::isEnabled() const {
    return enabled;
}

void ElaraCodeEditorWidget::setFocused(bool v) {
    focused = enabled && v;
}

bool ElaraCodeEditorWidget::isFocused() const {
    return focused;
}

void ElaraCodeEditorWidget::setFontSize(double size) {
    font_size   = size;
    line_height = size + 6.0;
    updateScrollbars();
}

double ElaraCodeEditorWidget::getFontSize() const {
    return font_size;
}

void ElaraCodeEditorWidget::setBreakpoint(int logical_line, bool v) {
    ensureDecorations(logical_line);
    decorations[logical_line].breakpoint = v;
}

bool ElaraCodeEditorWidget::hasBreakpoint(int logical_line) const {
    if (logical_line >= (int)decorations.length()) return false;
    return decorations[logical_line].breakpoint;
}

void ElaraCodeEditorWidget::setBookmark(int logical_line, bool v) {
    ensureDecorations(logical_line);
    decorations[logical_line].bookmark = v;
}

bool ElaraCodeEditorWidget::hasBookmark(int logical_line) const {
    if (logical_line >= (int)decorations.length()) return false;
    return decorations[logical_line].bookmark;
}

ElaraMouseCursor ElaraCodeEditorWidget::cursor() const {
    return enabled ? ELARA_CURSOR_TEXT : ELARA_CURSOR_DEFAULT;
}

ElaraMouseCursor ElaraCodeEditorWidget::cursorAt(double px, double py) const {
    // Children (scrollbars) take priority
    for (int i = (int)children.length() - 1; i >= 0; i--) {
        if (!children[i] || !children[i]->isVisible()) continue;
        if (children[i]->contains(px, py)) {
            return children[i]->cursorAt(
                px - children[i]->getX() - children[i]->getMarginLeft(),
                py - children[i]->getY() - children[i]->getMarginTop()
            );
        }
    }

    if (px >= minimapLeft() && px < minimapLeft() + minimap_width) {
        return ELARA_CURSOR_POINTER;
    }
    if (px >= editorLeft() && px < editorRight()) {
        return enabled ? ELARA_CURSOR_TEXT : ELARA_CURSOR_DEFAULT;
    }
    return ELARA_CURSOR_DEFAULT;
}

// ---------------------------------------------------------------------------
// draw()
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::draw(ElaraDrawContext* ctx) {
    // Ensure visible line map is populated on first draw
    if (visible_line_map.length() == 0 && value.length() >= 0) {
        rebuildFolds();
        rebuildVisibleLineMap();
    }

    // Determine visibility before any layout so effectiveScrollbar*() is correct.
    updateScrollbars();

    if (vertical_slider->isVisible()) {
        vertical_slider->setBounds(
            width - scrollbar_size, 0,
            scrollbar_size, height - effectiveScrollbarH()
        );
    }
    if (horizontal_slider->isVisible()) {
        horizontal_slider->setBounds(
            editorLeft(), height - scrollbar_size,
            editorContentWidth(), scrollbar_size
        );
    }

    rebuildViewportMetrics(ctx);
    drawGutter(ctx);
    drawEditor(ctx);
    drawMinimap(ctx);

    if (vertical_slider->isVisible()) {
        vertical_slider->onDraw(ctx, (int)scrollbar_size, (int)(height - effectiveScrollbarH()));
    }
    if (horizontal_slider->isVisible()) {
        horizontal_slider->onDraw(ctx, (int)editorContentWidth(), (int)scrollbar_size);
    }
}

// ---------------------------------------------------------------------------
// eventPropagate()
// ---------------------------------------------------------------------------

bool ElaraCodeEditorWidget::eventPropagate(ElaraUiEvent event) {
    if (vertical_slider->isVisible()) {
        vertical_slider->setBounds(
            width - scrollbar_size, 0,
            scrollbar_size, height - effectiveScrollbarH()
        );
    }
    if (horizontal_slider->isVisible()) {
        horizontal_slider->setBounds(
            editorLeft(), height - scrollbar_size,
            editorContentWidth(), scrollbar_size
        );
    }

    // While dragging the minimap, own all mouse move/up events so children
    // (scrollbars) cannot steal them even if the cursor drifts.
    if (minimap_dragging) {
        if (event.type == ELARA_UI_MOUSE_MOVE) {
            onMouseMove(event.x, event.y);
            return true;
        }
        if (event.type == ELARA_UI_MOUSE_UP) {
            onMouseUp(event.button, event.x, event.y);
            return true;
        }
    }

    bool handled = ElaraWidget::eventPropagate(event);

    if (vertical_slider->isVisible()) scroll_y = (int)vertical_slider->getValue();
    if (horizontal_slider->isVisible()) scroll_x = (int)horizontal_slider->getValue();
    clampScroll();

    return handled;
}

// ---------------------------------------------------------------------------
// onMouseDown()
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::onMouseDown(int button, double px, double py) {
    emitMouseDown(button, px, py);

    if (button != 1 || !enabled) return;

    // --- Gutter click ---
    if (px < gutter_width) {
        int logical = gutterLogicalLine(py);
        if (logical < 0) return;

        if (px < 12.0) {
            // Breakpoint toggle
            ensureDecorations(logical);
            decorations[logical].breakpoint = !decorations[logical].breakpoint;

        } else if (px < 22.0) {
            // Bookmark toggle
            ensureDecorations(logical);
            decorations[logical].bookmark = !decorations[logical].bookmark;

        } else if (px >= gutter_width - 16.0) {
            // Fold toggle
            FoldRegion* fold = foldStartingAt(logical);
            if (fold) {
                fold->collapsed = !fold->collapsed;
                rebuildVisibleLineMap();

                // If caret ended up inside a hidden region, move it to fold start
                int caret_logical = logicalLineForIndex(caret_index);
                if (isLineFoldedAway(caret_logical)) {
                    caret_index = indexForLineColumn(fold->start_line, 0);
                }

                clampScroll();
                updateScrollbars();
            }
        }
        return;
    }

    // --- Minimap click ---
    if (px >= minimapLeft() && px < minimapLeft() + minimap_width) {
        minimap_dragging = true;
        int vis_total = (int)visible_line_map.length();
        if (vis_total > 0) {
            double rel   = py / height;
            if (rel < 0.0) rel = 0.0;
            if (rel > 1.0) rel = 1.0;
            int target   = (int)(rel * vis_total) - viewportLineCount() / 2;
            if (target < 0) target = 0;
            scroll_y = target;
            clampScroll();
            updateScrollbars();
        }
        return;
    }

    // --- Editor content click ---
    if (px >= editorLeft() && px < editorRight() &&
        py >= 0 && py < height - scrollbar_size) {

        double text_px = px - editorLeft() - padding_x;
        caret_index = caretAtPoint(text_px, py);
        clampCaret();
        preferred_column = -1;
        setFocused(true);

        ElaraRootWidget* root = rootWidget();
        if (root) root->setFocus(getHandle());
    }
}

// ---------------------------------------------------------------------------
// onMouseMove() / onMouseUp()
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::onMouseMove(double px, double py) {
    if (!minimap_dragging) return;

    int vis_total = (int)visible_line_map.length();
    if (vis_total <= 0) return;

    double rel  = py / height;
    if (rel < 0.0) rel = 0.0;
    if (rel > 1.0) rel = 1.0;

    int target = (int)(rel * vis_total) - viewportLineCount() / 2;
    if (target < 0) target = 0;
    scroll_y = target;
    clampScroll();
    updateScrollbars();
}

void ElaraCodeEditorWidget::onMouseUp(int button, double px, double py) {
    (void)px; (void)py;
    if (button == 1) {
        minimap_dragging = false;
    }
}

// ---------------------------------------------------------------------------
// onKeyDown()
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::onKeyDown(unsigned int keyval) {
    if (!enabled || !focused) return;

    emitKeyDown(keyval);

    switch (keyval) {
        case ELARA_KEY_BACKSPACE:
            backspace();
            preferred_column = -1;
            break;

        case ELARA_KEY_DELETE:
            deleteForward();
            preferred_column = -1;
            break;

        case ELARA_KEY_LEFT:
            moveCaretHorizontal(-1);
            break;

        case ELARA_KEY_RIGHT:
            moveCaretHorizontal(1);
            break;

        case ELARA_KEY_UP:
            moveCaretVertical(-1);
            break;

        case ELARA_KEY_DOWN:
            moveCaretVertical(1);
            break;

        case ELARA_KEY_PAGE_UP:
            moveCaretVertical(-viewportLineCount());
            break;

        case ELARA_KEY_PAGE_DOWN:
            moveCaretVertical(viewportLineCount());
            break;

        case ELARA_KEY_HOME:
            caret_index = logicalLineStart(logicalLineForIndex(caret_index));
            preferred_column = -1;
            break;

        case ELARA_KEY_END:
            caret_index = logicalLineEnd(logicalLineForIndex(caret_index));
            preferred_column = -1;
            break;

        case ELARA_KEY_RETURN:
        case ELARA_KEY_KP_ENTER:
            insertText(String('\n'));
            preferred_column = -1;
            break;

        case ELARA_KEY_TAB:
            insertText(String("    "));
            preferred_column = -1;
            break;

        default:
            if (keyval >= 32 && keyval <= 126) {
                insertText(String((char)keyval));
                preferred_column = -1;
            }
            break;
    }

    clampCaret();
    scrollToCaret();
    updateScrollbars();
}

// ---------------------------------------------------------------------------
// onWidgetValueChanged()
// ---------------------------------------------------------------------------

void ElaraCodeEditorWidget::onWidgetValueChanged(
    ElaraWidgetHandle handle,
    double value_state
) {
    Memory mem = handle.getHandle();
    String handle_str((const char*)mem.getPtr(), mem.length());
    String my_str((const char*)getHandle().getHandle().getPtr(),
                  getHandle().getHandle().length());

    if (handle_str == my_str + String(".vscroll")) {
        scroll_y = (int)value_state;
        clampScroll();
        return;
    }
    if (handle_str == my_str + String(".hscroll")) {
        scroll_x = (int)value_state;
        clampScroll();
    }
}

} // namespace elara
