/**
 * @file Visualizer.h
 * @brief Cairo-based renderer for placement snapshots and metric plots (PNG export).
 */
#pragma once

#include "Common.h"

#ifdef CREATE_VISUALIZATION
#include <cairo/cairo.h>
#include <cmath>
#include "DataBase.h"
#include "Grid.h"
#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN 

struct PlotInfo {
    int iteration;
    float hpwl;
    float overflow;
    float step_length;
    float density_weight;
    std::string benchmark_name;
    std::string filename_override; // if set, use this instead of "iter_<N>"
    // Two-phase runs (TODO #13): without these the GIF shows the standard cells scattering back
    // to the die centre mid-animation with nothing to say it was the deliberate phase-2 re-seed.
    std::string phase_name;        // empty on single-phase runs -> phase row is omitted
    int phase_iteration = 0;       // iteration counted from the current phase's start
    // Density grid, for the zoom view's bin overlay. 0 => no bin grid (the full-die view never
    // draws one: at 512-2048 bins across a 2048 px canvas it is a solid wash).
    int bins_per_row = 0;
    int bins_per_col = 0;
};

/**
 * @brief The rectangle of the die a Visualizer renders, in die coordinates.
 *
 * The default (@p zoomed false, the whole die) reproduces the historical full-die frame exactly:
 * the coordinate map is `DIE_START + (v - xl) * DIE_SCALE / span`, which with xl = 0 and
 * span = the die size is the expression that was there before the window existed.
 *
 * A zoom view is the SAME drawing code over a smaller window (TODO #14). At MMS scale the full
 * die is 200k-1M cells in ~2000 px, so everything below the macro scale is a grey wash and the
 * row structure the placement is actually targeting is invisible. @p zoomed additionally enables
 * the detail layers -- row pitch, bin boundaries, per-cell outlines -- which are only legible
 * once a window is small enough to resolve individual cells.
 */
struct ViewWindow {
    float xl = 0.0f, yl = 0.0f, xh = 0.0f, yh = 0.0f;
    bool  zoomed = false;
};

class Visualizer
{
    private:

    // Member Data
    Box m_die_area;
    float m_die_width, m_die_height;
    float m_canvas_width, m_canvas_height;

    ViewWindow m_view;                    // the rendered region; whole die unless zoomed
    float m_view_width, m_view_height;    // m_view's span, the coordinate map's denominator

    const int MAX_CANVAS_PX = 2048; // longest dimension in pixels
    int m_canvas_px_w, m_canvas_px_h; // computed from view aspect ratio in init()
    const float DIE_SCALE = 0.80; // view occupies 80% of canvas
    const float DIE_START = (1 - DIE_SCALE) / 2; // 10% margin on all sides
    const float MIN_SIZE = 0.001; // Minimum size to be visible
    // A detail layer denser than this is a solid wash, so it is dropped rather than drawn --
    // the zoom window can be set to any span, including one too wide for rows or bins.
    const int MAX_DETAIL_LINES = 256;
    cairo_surface_t *m_surface;
    cairo_t *m_cairo_ctx;

    // Die coordinates -> canvas unit square, through the current view window.
    double mapX(double die_x) const { return DIE_START + (die_x - m_view.xl) * DIE_SCALE / m_view_width; }
    double mapW(double die_w) const { return die_w * DIE_SCALE / m_view_width; }
    double mapH(double die_h) const { return die_h * DIE_SCALE / m_view_height; }

    /**
     * @brief Die y -> canvas y, INVERTED: cairo's user-space y grows downward while die y grows
     *        upward. Without the inversion every frame is vertically mirrored, which it was until
     *        2026-08-05 — harmless at full-die scale, actively misleading at zoom, where a region
     *        has to be matched against a DEF or a row index.
     *
     * The flip lives in the arithmetic, not in a cairo transform, on purpose: `cairo_scale(1,-1)`
     * would mirror the overlay TEXT and every glyph with it. Everything drawn from canvas
     * coordinates (the overlay, the reticle, the view border) is therefore untouched.
     */
    double mapY(double die_y) const
    { return DIE_START + DIE_SCALE - (die_y - m_view.yl) * DIE_SCALE / m_view_height; }

    /// @brief Canvas y of the TOP edge of a die-space box. cairo_rectangle() grows downward from
    ///        its anchor, so after the y flip a box must be anchored at its top, not its bottom.
    double mapRectTop(double die_y, double die_ysize) const { return mapY(die_y + die_ysize); }

    // drawPlacement()'s steps, broken out for readability
    void drawRowLines(DataBase& db);
    void drawBinGrid(const PlotInfo& info);
    void drawFillerCells(DataBase& db);
    void drawFixedComponents(DataBase& db);
    void drawFrozenMacros(DataBase& db);
    void drawMovableStandardCells(DataBase& db);
    void drawMovableMacros(DataBase& db);
    void drawAllIOPads(DataBase& db);
    void drawFocusHighlights(DataBase& db);
    void drawPlacementInfoOverlay(const PlotInfo& info);
    void exportPlacementPNG(fs::path dir, const PlotInfo& info);
    /// @brief Thin black outline over the path just filled — zoom only, where individual cells
    ///        are big enough that an outline separates neighbours instead of blackening them.
    void outlineIfZoomed();

    public:

    // Constructor
    Visualizer() {};

    void init(Box die_area);                        // full-die view (the historical behaviour)
    void init(Box die_area, ViewWindow view);       // arbitrary window; see ViewWindow
    float scale(float f);   // normalized die fraction -> canvas, x axis
    float scaleY(float f);  // ... y axis, with the same inversion as mapY
    void drawComponent(Component* c);
    void drawIOPad(IOPad* p);
    void highlightNet(Net* net_p);
    void highlightNode(Node* node_p);
    void drawCross(float x, float y, float cross_size = 0.004);
    void drawReticle(float x, float y, float reticle_size = 0.008);
    void drawArrow(float x, float y, float x_mag, float y_mag);
    void drawPlacement(DataBase&, fs::path, PlotInfo);
    void drawElectricField(Grid&, fs::path, int);
};

/**
 * @brief Renders one run's scalar-metric histories (HPWL/overflow/step length/density weight)
 *        to PNG: either one metric per full-canvas chart (plotHistory) or all of them as
 *        vertically-stacked panels sharing one x-axis (plotStacked, TODO #18). Distinct from
 *        Visualizer above, which renders cell layouts, not line charts.
 */
class CairoPlotter {
public:
    /// @brief One metric's data plus how to draw it. `data` must outlive the plot call.
    struct Series {
        const std::vector<float>* data;
        std::string title;      // chart title (plotHistory) / panel header (plotStacked)
        std::string y_label;    // y-axis label
        double r, g, b;          // line color, [0,1]
        bool log_scale = false;  // log10 y-axis -- needed for density_weight (spans ~1e-12..1)
    };

    CairoPlotter(int w, int h) : width(w), height(h) {
        m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        m_cairo_ctx = cairo_create(m_surface);
        cairo_set_source_rgb(m_cairo_ctx, 1, 1, 1); // white background
        cairo_paint(m_cairo_ctx);
    }

    ~CairoPlotter() {
        cairo_destroy(m_cairo_ctx);
        cairo_surface_destroy(m_surface);
    }

    /// @brief One series, full canvas: title banner + one axis panel + rotated y-axis label.
    void plotHistory(const Series& s) {
        if (s.data->empty()) return;

        cairo_select_font_face(m_cairo_ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(m_cairo_ctx, TICK_FONT_SIZE);
        AxisTicks yaxis = computeYAxis(*s.data, s.log_scale, Y_TICK_COUNT);
        double left_margin = TICK_STRIP + maxTickWidth(m_cairo_ctx, yaxis) + TICK_GAP + TICK_MARK + 4;

        const double right_margin = 30, top_margin = 50, bottom_margin = 60;

        cairo_select_font_face(m_cairo_ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(m_cairo_ctx, 18);
        cairo_set_source_rgb(m_cairo_ctx, 0.05, 0.05, 0.05);
        std::string title = s.title + (s.log_scale ? " (log scale)" : "");
        cairo_text_extents_t extents;
        cairo_text_extents(m_cairo_ctx, title.c_str(), &extents);
        cairo_move_to(m_cairo_ctx, (width - extents.width) / 2, 30);
        cairo_show_text(m_cairo_ctx, title.c_str());

        drawPanel(m_cairo_ctx, left_margin, top_margin, width - right_margin, height - bottom_margin,
                  s, /*show_x_ticks=*/true, /*show_header=*/false);

        // Y-axis label, rotated, in the TICK_STRIP reserved for it -- always clear of the
        // (measured, not guessed) tick-label width baked into left_margin above.
        cairo_select_font_face(m_cairo_ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(m_cairo_ctx, 13);
        cairo_set_source_rgb(m_cairo_ctx, 0.1, 0.1, 0.1);
        cairo_text_extents(m_cairo_ctx, s.y_label.c_str(), &extents);
        cairo_save(m_cairo_ctx);
        cairo_translate(m_cairo_ctx, 18, (top_margin + height - bottom_margin) / 2);
        cairo_rotate(m_cairo_ctx, -M_PI / 2);
        cairo_move_to(m_cairo_ctx, -extents.width / 2, 0);
        cairo_show_text(m_cairo_ctx, s.y_label.c_str());
        cairo_restore(m_cairo_ctx);
    }

    void savePNG(const std::string& filename) {
        cairo_surface_write_to_png(m_surface, filename.c_str());
    }

    /// @brief All `panels` as vertically-stacked, equal-width axis panels sharing one x-axis
    ///        (array index -> iteration number). Replaces the old normalize-and-overlay
    ///        combined_history.png: overlaying differently-scaled series on one axis can
    ///        manufacture a correlation that isn't really there, which is a bad property for a
    ///        chart used to debug the algorithm. Builds and saves its own surface.
    static void plotStacked(const std::vector<Series>& panels, const std::string& title,
                             const std::string& filename) {
        if (panels.empty()) return;
        constexpr int WIDTH = 900, PANEL_H = 210, PANEL_GAP = 18, TOP_MARGIN = 58, BOTTOM_MARGIN = 55,
                      RIGHT_MARGIN = 30;
        int height = TOP_MARGIN + (int)panels.size() * PANEL_H + ((int)panels.size() - 1) * PANEL_GAP
                     + BOTTOM_MARGIN;

        cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIDTH, height);
        cairo_t* ctx = cairo_create(surface);
        cairo_set_source_rgb(ctx, 1, 1, 1);
        cairo_paint(ctx);

        // Shared left margin: wide enough for the WIDEST y-tick label across every panel, so
        // every panel's plot rect starts at the same x pixel and a feature at iteration N lines
        // up vertically across all four -- the entire point of sharing the x-axis.
        cairo_select_font_face(ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(ctx, TICK_FONT_SIZE);
        double max_tick_w = 0;
        for (auto& p : panels) {
            if (p.data->empty()) continue;
            max_tick_w = std::max(max_tick_w, maxTickWidth(ctx, computeYAxis(*p.data, p.log_scale, Y_TICK_COUNT)));
        }
        double x0 = TICK_STRIP + max_tick_w + TICK_GAP + TICK_MARK + 4;
        double x1 = WIDTH - RIGHT_MARGIN;

        cairo_select_font_face(ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(ctx, 18);
        cairo_set_source_rgb(ctx, 0.05, 0.05, 0.05);
        cairo_text_extents_t extents;
        cairo_text_extents(ctx, title.c_str(), &extents);
        cairo_move_to(ctx, (WIDTH - extents.width) / 2, 32);
        cairo_show_text(ctx, title.c_str());

        for (size_t i = 0; i < panels.size(); ++i) {
            double y0 = TOP_MARGIN + i * (PANEL_H + PANEL_GAP);
            double y1 = y0 + PANEL_H;
            bool is_last = (i + 1 == panels.size()); // only the bottom panel labels the shared x-axis
            drawPanel(ctx, x0, y0, x1, y1, panels[i], is_last, /*show_header=*/true);
        }

        cairo_surface_write_to_png(surface, filename.c_str());
        cairo_destroy(ctx);
        cairo_surface_destroy(surface);
    }

private:
    cairo_surface_t* m_surface;
    cairo_t* m_cairo_ctx;
    int width, height;

    static constexpr const char* FONT_FAMILY = "DejaVu Sans"; // resolved via fontconfig; matches
                                                                // tools/generate_viz.py's renderer
    static constexpr int X_TICK_COUNT = 8;
    static constexpr int Y_TICK_COUNT = 5;
    static constexpr double TICK_FONT_SIZE = 10;
    static constexpr double TICK_STRIP = 22; // reserved strip for the rotated y-axis label/swatch
    static constexpr double TICK_GAP = 8;
    static constexpr double TICK_MARK = 5;

    /// @brief Y-axis bounds (in "mapped" space: log10(value) if log_scale, else value) plus a
    ///        tick set, padded 10% beyond the data range in that same mapped space -- padding a
    ///        log-scale panel in linear value space would pad the wrong (compressed/expanded) end.
    struct AxisTicks { double lo, hi; std::vector<std::pair<double, std::string>> ticks; };

    static double mapValue(double raw, bool log_scale) {
        return log_scale ? std::log10(std::max(raw, 1e-30)) : raw;
    }

    static AxisTicks computeYAxis(const std::vector<float>& data, bool log_scale, int max_ticks) {
        auto minmax = std::minmax_element(data.begin(), data.end());
        double lo = mapValue(*minmax.first, log_scale);
        double hi = mapValue(*minmax.second, log_scale);
        double range = hi - lo;
        if (range <= 0) range = std::max(std::abs(hi), 1.0); // flat series: still show a visible band
        lo -= range * 0.1;
        hi += range * 0.1;

        std::vector<std::pair<double, std::string>> ticks;
        char buf[32];
        if (log_scale) {
            // One tick per decade (or every `step` decades if the range spans more than
            // max_ticks of them) rather than max_ticks ticks evenly spaced in log space --
            // fractional-decade labels ("1e-3.4") are not a reader-friendly log axis.
            int decade_lo = (int)std::floor(lo), decade_hi = (int)std::ceil(hi);
            int step = std::max(1, (int)std::ceil((double)(decade_hi - decade_lo) / max_ticks));
            for (int d = decade_lo; d <= decade_hi; d += step) {
                if (d < lo || d > hi) continue;
                snprintf(buf, sizeof(buf), "1e%d", d);
                ticks.push_back({(double)d, buf});
            }
        } else {
            for (int i = 0; i <= max_ticks; ++i) {
                double v = lo + i * (hi - lo) / max_ticks;
                snprintf(buf, sizeof(buf), "%.2e", v);
                ticks.push_back({v, buf});
            }
        }
        return {lo, hi, ticks};
    }

    /// @brief Widest rendered tick label, in the font already selected on `ctx`. Callers use
    ///        this to size the left margin so the y-axis title never overlaps a tick value --
    ///        the bug in the original fixed-offset layout.
    static double maxTickWidth(cairo_t* ctx, const AxisTicks& axis) {
        double max_w = 0;
        cairo_text_extents_t extents;
        for (auto& t : axis.ticks) {
            cairo_text_extents(ctx, t.second.c_str(), &extents);
            max_w = std::max(max_w, extents.width);
        }
        return max_w;
    }

    /// @brief Draws one series (grid, border, y-ticks, line, points) into the rect
    ///        [x0,y0]-[x1,y1] of `ctx`. `show_x_ticks` draws the shared "Iteration" axis below
    ///        the rect (only the bottom panel of a stack needs it); `show_header` draws a
    ///        colored swatch + metric name inside the top-left corner instead of a separate
    ///        rotated y-axis label (used by plotStacked, where 4 rotated labels would be clutter).
    static void drawPanel(cairo_t* ctx, double x0, double y0, double x1, double y1,
                           const Series& s, bool show_x_ticks, bool show_header) {
        const auto& data = *s.data;
        if (data.empty()) return;

        AxisTicks yaxis = computeYAxis(data, s.log_scale, Y_TICK_COUNT);
        double x_min = 0, x_max = std::max(1.0, (double)data.size() - 1);
        double plot_w = x1 - x0, plot_h = y1 - y0;
        int n_xticks = std::min(X_TICK_COUNT, std::max(1, (int)data.size() - 1));

        auto pxX = [&](double idx) { return x0 + (idx - x_min) / (x_max - x_min) * plot_w; };
        auto pxY = [&](float raw) {
            double v = mapValue(raw, s.log_scale);
            return y1 - (v - yaxis.lo) / (yaxis.hi - yaxis.lo) * plot_h;
        };

        // Grid: horizontal at each y-tick, vertical at each x-tick.
        cairo_set_line_width(ctx, 1.0);
        cairo_set_source_rgb(ctx, 0.88, 0.88, 0.86);
        for (auto& t : yaxis.ticks) {
            double py = y1 - (t.first - yaxis.lo) / (yaxis.hi - yaxis.lo) * plot_h;
            cairo_move_to(ctx, x0, py); cairo_line_to(ctx, x1, py); cairo_stroke(ctx);
        }
        for (int i = 0; i <= n_xticks; ++i) {
            double px = pxX(x_min + i * (x_max - x_min) / n_xticks);
            cairo_move_to(ctx, px, y0); cairo_line_to(ctx, px, y1); cairo_stroke(ctx);
        }

        // Panel border
        cairo_set_source_rgb(ctx, 0.6, 0.6, 0.57);
        cairo_set_line_width(ctx, 1.2);
        cairo_rectangle(ctx, x0, y0, plot_w, plot_h);
        cairo_stroke(ctx);

        // Y-axis ticks + labels (every panel: each has its own scale)
        cairo_select_font_face(ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(ctx, TICK_FONT_SIZE);
        for (auto& t : yaxis.ticks) {
            double py = y1 - (t.first - yaxis.lo) / (yaxis.hi - yaxis.lo) * plot_h;
            cairo_text_extents_t extents;
            cairo_text_extents(ctx, t.second.c_str(), &extents);
            cairo_set_source_rgb(ctx, 0.15, 0.15, 0.15);
            cairo_move_to(ctx, x0 - extents.width - TICK_GAP, py + extents.height / 2);
            cairo_show_text(ctx, t.second.c_str());
            cairo_set_source_rgb(ctx, 0.4, 0.4, 0.4);
            cairo_move_to(ctx, x0 - TICK_MARK, py); cairo_line_to(ctx, x0, py); cairo_stroke(ctx);
        }

        // X-axis ticks + labels: shared axis, only drawn once per stack (or always for a
        // standalone chart). Labels are 1-based iteration numbers (array index + 1), not the
        // raw array index -- and, unlike iterations.dat's own "Iter" column, immune to a
        // phase-2 restart's iteration renumbering since these vectors are appended once per
        // call across the whole run regardless of phase.
        if (show_x_ticks) {
            for (int i = 0; i <= n_xticks; ++i) {
                double idx = x_min + i * (x_max - x_min) / n_xticks;
                double px = pxX(idx);
                cairo_set_source_rgb(ctx, 0.4, 0.4, 0.4);
                cairo_move_to(ctx, px, y1); cairo_line_to(ctx, px, y1 + TICK_MARK); cairo_stroke(ctx);
                std::string label = std::to_string((int)std::lround(idx) + 1);
                cairo_text_extents_t extents;
                cairo_text_extents(ctx, label.c_str(), &extents);
                cairo_set_source_rgb(ctx, 0.15, 0.15, 0.15);
                cairo_move_to(ctx, px - extents.width / 2, y1 + 18);
                cairo_show_text(ctx, label.c_str());
            }
            cairo_select_font_face(ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(ctx, 12);
            cairo_set_source_rgb(ctx, 0.05, 0.05, 0.05);
            cairo_text_extents_t extents;
            cairo_text_extents(ctx, "Iteration", &extents);
            cairo_move_to(ctx, x0 + (plot_w - extents.width) / 2, y1 + 36);
            cairo_show_text(ctx, "Iteration");
        }

        // Panel header: colored swatch + metric name, top-left inside the plot rect (plotStacked
        // only -- plotHistory's standalone charts already carry the title above the panel).
        if (show_header) {
            std::string header = s.title + (s.log_scale ? " (log)" : "");
            cairo_set_source_rgb(ctx, s.r, s.g, s.b);
            cairo_rectangle(ctx, x0 + 8, y0 + 8, 10, 10);
            cairo_fill(ctx);
            cairo_select_font_face(ctx, FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(ctx, 13);
            cairo_set_source_rgb(ctx, 0.05, 0.05, 0.05);
            cairo_move_to(ctx, x0 + 24, y0 + 17);
            cairo_show_text(ctx, header.c_str());
        }

        // Data line + point markers, in the series' own color.
        cairo_set_source_rgb(ctx, s.r, s.g, s.b);
        cairo_set_line_width(ctx, 2.0);
        bool first = true;
        for (size_t i = 0; i < data.size(); ++i) {
            double px = pxX((double)i), py = pxY(data[i]);
            if (first) { cairo_move_to(ctx, px, py); first = false; }
            else cairo_line_to(ctx, px, py);
        }
        cairo_stroke(ctx);

        cairo_set_source_rgb(ctx, s.r * 0.7, s.g * 0.7, s.b * 0.7);
        for (size_t i = 0; i < data.size(); ++i) {
            cairo_arc(ctx, pxX((double)i), pxY(data[i]), 2.5, 0, 2 * M_PI);
            cairo_fill(ctx);
        }
    }
};


AIEPLACE_NAMESPACE_END

#endif
