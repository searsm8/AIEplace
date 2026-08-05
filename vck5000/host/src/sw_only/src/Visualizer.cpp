/**
 * @file Visualizer.cpp
 * @brief Cairo-based rendering of placements and metric plots (PNG export for the run gif).
 */
#include "Visualizer.h"
#include <cmath>

#ifdef CREATE_VISUALIZATION

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h) 

/// @brief Full-die view. The window is anchored at the ORIGIN, not at the die's lower-left
///        corner, because that is what every coordinate map here has always done — node
///        positions are already in a frame whose origin is the die corner (the bookshelf
///        die_shift). Keeping it makes this view byte-identical to the pre-zoom renderer.
void Visualizer::init(Box die_area)
{
    ViewWindow whole_die;
    whole_die.xh = die_area.getXsize();
    whole_die.yh = die_area.getYsize();
    init(die_area, whole_die);
}

/**
 * @brief Build the canvas for @p view — the whole die, or a zoom window (TODO #14).
 *
 * The canvas aspect follows the VIEW, not the die, so a square zoom window gets a square image
 * whatever the die's shape. Everything downstream draws through mapX/mapY, so the only thing
 * that changes between the two views is this window.
 */
void Visualizer::init(Box die_area, ViewWindow view)
{
    m_die_area = die_area;
    m_die_width = die_area.getXsize();
    m_die_height= die_area.getYsize();

    m_view = view;
    m_view_width  = std::max(1e-6f, view.xh - view.xl);
    m_view_height = std::max(1e-6f, view.yh - view.yl);

    m_canvas_width = m_die_width * 1.2;
    m_canvas_height = m_die_height * 1.2;

    // Scale canvas pixels to match view aspect ratio
    if (m_view_width >= m_view_height) {
        m_canvas_px_w = MAX_CANVAS_PX;
        m_canvas_px_h = std::max(1, (int)(MAX_CANVAS_PX * m_view_height / m_view_width));
    } else {
        m_canvas_px_h = MAX_CANVAS_PX;
        m_canvas_px_w = std::max(1, (int)(MAX_CANVAS_PX * m_view_width / m_view_height));
    }

    m_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, m_canvas_px_w, m_canvas_px_h);
    m_cairo_ctx = cairo_create (m_surface);
    cairo_scale (m_cairo_ctx, m_canvas_px_w, m_canvas_px_h);
}

/// @brief Normalized die fraction [0,1] -> canvas, x axis. The FULL-DIE map; it ignores the view
///        window, so it is valid only on a full-die Visualizer (its one caller,
///        drawElectricField, is one).
float Visualizer::scale(float f) {
    return DIE_START + DIE_SCALE*f;
}

/// @brief The y counterpart of scale(), carrying the same inversion as mapY — die y up, canvas y
///        down. Kept in step with mapY so the field arrows and the cells they act on agree.
float Visualizer::scaleY(float f) {
    return DIE_START + DIE_SCALE*(1.0f - f);
}

void Visualizer::outlineIfZoomed()
{
    if (!m_view.zoomed) { cairo_fill(m_cairo_ctx); return; }
    cairo_fill_preserve(m_cairo_ctx);
    cairo_set_source_rgb (m_cairo_ctx, 0.15, 0.15, 0.15);
    cairo_set_line_width (m_cairo_ctx, 0.0006);
    cairo_stroke(m_cairo_ctx);
}

// Both of these anchor at the cell's TOP-LEFT in canvas space (mapRectTop), not its die
// lower-left: cairo_rectangle grows downward. When MIN_SIZE floors the height, the box therefore
// hangs below the true top edge rather than sitting on the true bottom edge — sub-pixel at the
// canvas sizes here, and only ever for cells too small to see at their true size anyway.
void Visualizer::drawComponent(Component* c)
{
    double x = mapX((double) c->getX());
    double y = mapRectTop((double) c->getY(), c->getYsize());
    double width = max<double>(MIN_SIZE, mapW(c->getXsize()));
    double height =max<double>(MIN_SIZE, mapH(c->getYsize()));
    cairo_rectangle (m_cairo_ctx, x, y, width, height);
}

void Visualizer::drawIOPad(IOPad* p)
{
    double x = mapX((double) p->getX());
    double y = mapRectTop((double) p->getY(), p->getYsize());
    double width = max<double>(MIN_SIZE, mapW(p->getXsize()));
    double height =max<double>(MIN_SIZE, mapH(p->getYsize()));
    cairo_rectangle (m_cairo_ctx, x, y, width, height);
}

void Visualizer::highlightNet(Net* net_p)
{
    Box bb = net_p->getBoundingBox();
    // draw a rect around the net
    cairo_set_source_rgb (m_cairo_ctx, 1.0, 1.0, 0.0); // bright yellow
    cairo_set_line_width (m_cairo_ctx, 0.002);
    cairo_rectangle (m_cairo_ctx, mapX(bb.getPosBottomLeft().x),                    // x
                         mapRectTop(bb.getPosBottomLeft().y, bb.getYsize()),        // y (top)
                         mapW(bb.getXsize()),                                       // width
                         mapH(bb.getYsize()));                                      // height
    cairo_stroke(m_cairo_ctx);

    // draw X's for each pin location (node + offset)
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black
    float sum_X = 0;
    float sum_Y = 0;
    for(const NetPin& pin : net_p->getPins()) {
        Position p = pin.getPos();
        sum_X += p.x;
        sum_Y += p.y;
        drawCross(mapX(p.x), mapY(p.y));
    }
    cairo_stroke(m_cairo_ctx);


    // draw rat's nest of connecting wires!
    double avg_X = mapX(sum_X / net_p->getPins().size());
    double avg_Y = mapY(sum_Y / net_p->getPins().size());
    cairo_set_line_width (m_cairo_ctx, 0.0004); // very thin lines for rat's nest
    for(const NetPin& pin : net_p->getPins()) {
        Position p = pin.getPos();
        cairo_move_to(m_cairo_ctx, mapX(p.x), mapY(p.y));
        cairo_line_to(m_cairo_ctx, avg_X, avg_Y);
    }
    cairo_stroke(m_cairo_ctx);
}

void  Visualizer::highlightNode(Node* node_p)
{
    for(Net* net_p : node_p->getNets())
        highlightNet(net_p);

    // draw a red cross on the highlighted node
    cairo_set_source_rgb (m_cairo_ctx, 1.0, 0.0, 0.0); // red
    double node_X = mapX(node_p->getX());
    double node_Y = mapY(node_p->getY());
    drawCross(node_X, node_Y);
    cairo_arc(m_cairo_ctx, node_X, node_Y, .002, 0, 2 * M_PI);
    cairo_stroke(m_cairo_ctx);

    // Write the highlighted net's name in bottom left corner
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black
    cairo_select_font_face (m_cairo_ctx, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (m_cairo_ctx, .02);
    cairo_move_to (m_cairo_ctx, .1, .99);
    //std::string highlight_str = "Focus Node: " + node->getName();
    //cairo_show_text (cr, highlight_str.c_str());
    cairo_move_to (m_cairo_ctx, .6, .99);
    cairo_stroke(m_cairo_ctx);
}

void Visualizer::drawCross(float x, float y, float cross_size)
{
    // draw small cross at (x, y)
    // assumes color has already been set!
    cairo_set_line_width (m_cairo_ctx, 0.001);
    cairo_move_to(m_cairo_ctx, x - cross_size, y - cross_size);  
    cairo_line_to(m_cairo_ctx, x + cross_size, y + cross_size);  
    cairo_move_to(m_cairo_ctx, x + cross_size, y - cross_size);  
    cairo_line_to(m_cairo_ctx, x - cross_size, y + cross_size);  
    cairo_stroke(m_cairo_ctx);
}

void Visualizer::drawReticle(float x, float y, float reticle_size)
{
    cairo_set_line_width (m_cairo_ctx, 0.001);
    cairo_move_to(m_cairo_ctx, x - reticle_size, y);
    cairo_line_to(m_cairo_ctx, x + reticle_size, y);
    cairo_move_to(m_cairo_ctx, x, y - reticle_size);
    cairo_line_to(m_cairo_ctx, x, y + reticle_size);
    cairo_stroke(m_cairo_ctx);
}

void Visualizer::drawArrow(float x, float y, float x_mag, float y_mag)
{
    cairo_set_line_width (m_cairo_ctx, 0.001);
    // draw body of arrow
    cairo_move_to(m_cairo_ctx, x - x_mag, y - y_mag);  
    cairo_line_to(m_cairo_ctx, x + x_mag, y + y_mag);  
    cairo_stroke(m_cairo_ctx);
    
    // draw head of arrow
    cairo_arc(m_cairo_ctx, x + x_mag, y + y_mag, .002, 0, 2 * M_PI);
    cairo_fill(m_cairo_ctx);

}

void Visualizer::drawPlacement(DataBase& db, fs::path dir, PlotInfo info)
{
    Logger::log_detail("Exporting placement visualization to PNG...");
    // Draw items from back to front in order of Fillers, Components, Pins, Nets, Focus Nets, Focus Nodes
    // Start with a white background
    cairo_set_source_rgb (m_cairo_ctx, 1.0, 1.0, 1.0); // white
    cairo_paint(m_cairo_ctx);

    // Draw view boundary in black
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0);
    cairo_set_line_width (m_cairo_ctx, 0.004);
    cairo_rectangle (m_cairo_ctx, DIE_START, DIE_START, DIE_SCALE, DIE_SCALE);
    cairo_stroke(m_cairo_ctx);

    // Clip the geometry to the window — but ONLY when zoomed. In the full-die view, FIXED
    // terminals and blockages legitimately sit in the margin outside the core-row die (TODO #4),
    // and clipping would silently hide them.
    if (m_view.zoomed) {
        cairo_save(m_cairo_ctx);
        cairo_rectangle (m_cairo_ctx, DIE_START, DIE_START, DIE_SCALE, DIE_SCALE);
        cairo_clip(m_cairo_ctx);

        // Under the cells, so the cells read against them: the two structures a continuous
        // density field hides — the standard-cell rows the layout must land on, and the bin
        // grid the density is actually measured over.
        drawRowLines(db);
        drawBinGrid(info);
    }

    drawFillerCells(db);
    drawFixedComponents(db);
    drawFrozenMacros(db);

    // Movable cells split by kind: std cells blue, movable macros red. Classification is
    // Node::isMovableMacro() — the design's single macro definition (Placer::tagMovableMacros,
    // XPlace is_mov_macro) — rather than a die-area threshold recomputed here, so the picture
    // shows the same macro set the placer and the filler math act on.
    drawMovableStandardCells(db);
    drawMovableMacros(db);

    drawAllIOPads(db);
    drawFocusHighlights(db);

    if (m_view.zoomed) cairo_restore(m_cairo_ctx); // overlay text must not be clipped

    // draw reticle in center
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black
    drawReticle(0.5, 0.5);

    drawPlacementInfoOverlay(info);
    exportPlacementPNG(dir, info);
}

/**
 * @brief Standard-cell row pitch — zoom view only.
 *
 * This is the point of the zoom (TODO #14): global placement is easy to reason about as a
 * continuous density field and forget that the target is a ROW-BASED layout. Rows are drawn on a
 * uniform pitch from the origin, which is the row grid sw_only implicitly targets — it has no
 * per-row site model, and 11 of 16 MMS designs actually have a ragged core (TODO #3), so this is
 * the placer's view of the rows, not the benchmark's.
 */
void Visualizer::drawRowLines(DataBase& db)
{
    const float row_h = db.getRowHeight();
    if (row_h <= 0.0f) return;   // input supplied no row height

    const long first = (long)std::floor(m_view.yl / row_h);
    const long last  = (long)std::ceil (m_view.yh / row_h);
    if (last - first > MAX_DETAIL_LINES) return;

    cairo_set_source_rgb (m_cairo_ctx, 0.80, 0.80, 0.88);
    cairo_set_line_width (m_cairo_ctx, 0.0008);
    for (long row = first; row <= last; row++) {
        double y = mapY(row * (double)row_h);
        cairo_move_to(m_cairo_ctx, DIE_START, y);
        cairo_line_to(m_cairo_ctx, DIE_START + DIE_SCALE, y);
    }
    cairo_stroke(m_cairo_ctx);
}

/// @brief Density-bin boundaries — zoom view only, and only when the caller supplied the grid
///        size. Shows how many cells share a bin, i.e. what the density term can actually resolve.
void Visualizer::drawBinGrid(const PlotInfo& info)
{
    if (info.bins_per_row <= 0 || info.bins_per_col <= 0) return;
    const double bin_w = (double)m_die_width  / info.bins_per_row;
    const double bin_h = (double)m_die_height / info.bins_per_col;

    const long col_lo = (long)std::floor(m_view.xl / bin_w), col_hi = (long)std::ceil(m_view.xh / bin_w);
    const long row_lo = (long)std::floor(m_view.yl / bin_h), row_hi = (long)std::ceil(m_view.yh / bin_h);
    if (col_hi - col_lo > MAX_DETAIL_LINES || row_hi - row_lo > MAX_DETAIL_LINES) return;

    cairo_set_source_rgb (m_cairo_ctx, 0.62, 0.78, 0.62);
    cairo_set_line_width (m_cairo_ctx, 0.0012);
    for (long col = col_lo; col <= col_hi; col++) {
        double x = mapX(col * bin_w);
        cairo_move_to(m_cairo_ctx, x, DIE_START);
        cairo_line_to(m_cairo_ctx, x, DIE_START + DIE_SCALE);
    }
    for (long row = row_lo; row <= row_hi; row++) {
        double y = mapY(row * bin_h);
        cairo_move_to(m_cairo_ctx, DIE_START, y);
        cairo_line_to(m_cairo_ctx, DIE_START + DIE_SCALE, y);
    }
    cairo_stroke(m_cairo_ctx);
}

void Visualizer::drawFillerCells(DataBase& db)
{
    for (const auto& item : db.getFillers()) {
       drawComponent(item);
    }
    // Grey against the blue standard cells: at zoom, where the filler distribution is legible
    // for the first time, the two must be told apart — a filler is whitespace the placer is
    // holding open, not a cell.
    cairo_set_source_rgb (m_cairo_ctx, 0.9, 0.9, 0.9);  // grey
    outlineIfZoomed();
}

void Visualizer::drawFixedComponents(DataBase& db)
{
    for (const auto& item : db.getComponents()) {
       // Macros frozen by phase 2 are also FIXED by now; drawFrozenMacros() paints those.
       if (item.second->getStatus() == FIXED && !item.second->isMovableMacro())
           drawComponent(item.second);
    }
    cairo_set_source_rgb (m_cairo_ctx, 0.8, 0.0, 0.0); // red for fixed components
    cairo_fill_preserve(m_cairo_ctx);
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black border
    cairo_set_line_width (m_cairo_ctx, 0.001);
    cairo_stroke(m_cairo_ctx);
}

/**
 * @brief Macros that phase 2 froze — FIXED status, but still carrying the is_movable_macro tag.
 *
 * Distinct colour on purpose: these are the placer's own output (phase 1 placed them, the LP
 * legalized them), not part of the input floorplan. Painting them the same red as a pre-existing
 * blockage makes phase 2's deliverable unreadable on designs that HAVE blockages — adaptec2 has
 * 566 fixed components covering 60% of the die.
 */
void Visualizer::drawFrozenMacros(DataBase& db)
{
    bool any = false;
    for (const auto& item : db.getComponents()) {
       Component* c = item.second;
       if (c->getStatus() != FIXED) continue;
       if (!c->isMovableMacro())     continue;
       drawComponent(c);
       any = true;
    }
    if (!any) return;   // cairo_fill on an empty path would consume the caller's next path
    cairo_set_source_rgb (m_cairo_ctx, 0.55, 0.0, 0.55); // purple for frozen (phase-2) macros
    cairo_fill_preserve(m_cairo_ctx);
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black border, as for other fixed geometry
    cairo_set_line_width (m_cairo_ctx, 0.001);
    cairo_stroke(m_cairo_ctx);
}

void Visualizer::drawMovableStandardCells(DataBase& db)
{
    for (const auto& item : db.getComponents()) {
       Component* c = item.second;
       if (c->getStatus() == FIXED) continue;
       if (c->isMovableMacro()) continue;   // macros drawn red below
       drawComponent(c);
    }
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 1.0); // blue
    outlineIfZoomed();
}

void Visualizer::drawMovableMacros(DataBase& db)
{
    for (const auto& item : db.getComponents()) {
       Component* c = item.second;
       if (c->getStatus() == FIXED) continue;
       if (!c->isMovableMacro()) continue;
       drawComponent(c);
    }
    cairo_set_source_rgb (m_cairo_ctx, 1.0, 0.0, 0.0); // red for movable macros
    cairo_fill(m_cairo_ctx);
}

void Visualizer::drawAllIOPads(DataBase& db)
{
    for (const auto& item : db.getIOPads())
        drawIOPad(item.second);

    cairo_set_source_rgb (m_cairo_ctx, 1.0, 0.64, 0.0); // orange
    cairo_fill(m_cairo_ctx);
}

void Visualizer::drawFocusHighlights(DataBase& db)
{
    for(Net* net_p : db.getFocusNets())
        highlightNet(net_p);

    for(Node* node_p : db.getFocusNodes())
        highlightNode(node_p);
}

void Visualizer::drawPlacementInfoOverlay(const PlotInfo& info)
{
    // print current iteration and other info at bottom of image
    cairo_select_font_face (m_cairo_ctx, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (m_cairo_ctx, .02);

    cairo_move_to (m_cairo_ctx, .01, .04);
    std::string bench_str = "Benchmark: " + info.benchmark_name;
    cairo_show_text (m_cairo_ctx, bench_str.c_str());

    // Optional header lines, stacked under the benchmark. Each is its OWN line rather than a
    // suffix on the one above: at font size .02 a line runs off the canvas somewhere past ~70
    // characters, and cairo neither wraps nor warns — it just draws into the void.
    double header_y = .075;
    const double HEADER_LINE = .035;

    // Phase banner, only on runs that have one. Without it the phase-2 re-seed reads as a
    // divergence: every standard cell jumps back to the die centre between two frames.
    if (!info.phase_name.empty()) {
        cairo_move_to (m_cairo_ctx, .01, header_y);
        std::string phase_str = "Phase: " + info.phase_name +
                                " (phase iter " + std::to_string(info.phase_iteration) + ")";
        cairo_show_text (m_cairo_ctx, phase_str.c_str());
        header_y += HEADER_LINE;
    }

    // Where on the die this window is, and how much of it. Without this a zoom frame is
    // unreadable — a few hundred cells with nothing to locate them by.
    if (m_view.zoomed) {
        cairo_move_to (m_cairo_ctx, .01, header_y);
        std::string zoom_str = "Zoom: " + PREC_P(100.0f * m_view_width  / m_die_width,  2) + "% x "
                                        + PREC_P(100.0f * m_view_height / m_die_height, 2)
                             + "% of die @ (" + SCI(m_view.xl) + ", " + SCI(m_view.yl) + ")";
        cairo_show_text (m_cairo_ctx, zoom_str.c_str());
    }

    cairo_move_to (m_cairo_ctx, .01, .99);
    std::string iter_str = "Iter: " + std::to_string(info.iteration);
    cairo_show_text (m_cairo_ctx, iter_str.c_str());

    cairo_move_to (m_cairo_ctx, .18, .99);
    std::string hpwl_str = "HPWL: " + SCI(info.hpwl);
    cairo_show_text (m_cairo_ctx, hpwl_str.c_str());

    cairo_move_to (m_cairo_ctx, .40, .99);
    std::string ovfw_str = "OVFW: " + PREC_P(info.overflow, 2);
    cairo_show_text (m_cairo_ctx, ovfw_str.c_str());

    if (info.filename_override.empty()) {
        cairo_move_to (m_cairo_ctx, .55, .99);
        std::string alpha_str = "alpha: " + SCI(info.step_length);
        cairo_show_text (m_cairo_ctx, alpha_str.c_str());

        cairo_move_to (m_cairo_ctx, .78, .99);
        std::string lambda_str = "lambda: " + SCI(info.density_weight);
        cairo_show_text (m_cairo_ctx, lambda_str.c_str());
    }

    cairo_stroke(m_cairo_ctx);
}

void Visualizer::exportPlacementPNG(fs::path dir, const PlotInfo& info)
{
    fs::create_directories(dir); // ensure this directory exists
    string filename;
    if (!info.filename_override.empty()) {
        filename = info.filename_override + ".png";
    } else {
        filename = "iter_" + std::to_string(info.iteration) + ".png";
    }
    dir.append(filename);
    Table t;
    t.add_row(RowStream{} << "VISUALIZER output PNG to ");
    t.add_row(RowStream{} << dir);
    Logger::log_detail(t);
    cairo_surface_write_to_png (m_surface, dir.c_str());
}

void Visualizer::drawElectricField(Grid& grid, fs::path dir, int iteration)
{
    // Start with a white background
    //cairo_set_source_rgb (cr, 1.0, 1.0, 1.0); // white
    //cairo_paint(cr);

    // Draw bin reticles
    int bpr = grid.getBinsPerRow();
    int bpc = grid.getBinsPerCol();
    cairo_set_source_rgb (m_cairo_ctx, 0.75, 0.75, 0.75); // grey
    for(int i = 1; i < bpc; i++) {
        for(int j = 1; j < bpr; j++) {
            drawReticle(scale(float(j)/bpr), scaleY(float(i)/bpc), 0.004);
        }
    }

    // Draw arrows in middle of bins
    cairo_set_source_rgb (m_cairo_ctx, 0.05, 0.05, 0.05); // black
    float max_eField = 0;
    for(int i = 0; i < bpc; i++) {
        for(int j = 0; j < bpr; j++) {
            Bin bin = grid.getBin(i, j);
            if(bin.eField.x > max_eField) max_eField = bin.eField.x;
            if(bin.eField.y > max_eField) max_eField = bin.eField.y;
        }
    }
    for(int i = 0; i < bpc; i++) {
        for(int j = 0; j < bpr; j++) {
            Bin bin = grid.getBin(i, j);
            float x_mag = (0.2f/bpr) * std::atan(bin.eField.x / max_eField ); // use arctan function for asymptotes
            float y_mag = (0.2f/bpr) * std::atan(bin.eField.y / max_eField );
            // -y_mag: canvas y runs opposite to die y (see mapY), so a field pushing cells
            // toward larger die y has to draw as an arrow pointing UP the image.
            drawArrow(scale((j+.5f)/bpr), scaleY((i+.5f)/bpc), x_mag, -y_mag);
        }
    }

    // Export image
    fs::create_directories(dir); // ensure this directory exists
    string filename = "iter_";
    filename.append(std::to_string(iteration));
    filename.append(".png");
    dir.append(filename);
    Table t;
    t.add_row(RowStream{} << "VISUALIZER output E-field to ");
    t.add_row(RowStream{} << dir);
    Logger::log_detail(t);
    cairo_surface_write_to_png (m_surface, dir.c_str());

}

AIEPLACE_NAMESPACE_END

#endif