/**
 * @file Visualizer.cpp
 * @brief Cairo-based rendering of placements and metric plots (PNG export for the run gif).
 */
#include "Visualizer.h"

#ifdef CREATE_VISUALIZATION

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h) 

void Visualizer::init(Box die_area)
{
    m_die_area = die_area;
    m_die_width = die_area.getXsize();
    m_die_height= die_area.getYsize();

    m_canvas_width = m_die_width * 1.2;
    m_canvas_height = m_die_height * 1.2;

    // Scale canvas pixels to match die aspect ratio
    if (m_die_width >= m_die_height) {
        m_canvas_px_w = MAX_CANVAS_PX;
        m_canvas_px_h = std::max(1, (int)(MAX_CANVAS_PX * m_die_height / m_die_width));
    } else {
        m_canvas_px_h = MAX_CANVAS_PX;
        m_canvas_px_w = std::max(1, (int)(MAX_CANVAS_PX * m_die_width / m_die_height));
    }

    m_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, m_canvas_px_w, m_canvas_px_h);
    m_cairo_ctx = cairo_create (m_surface);
    cairo_scale (m_cairo_ctx, m_canvas_px_w, m_canvas_px_h);
}

float Visualizer::scale(float f) {
    return DIE_START + DIE_SCALE*f;
}


void Visualizer::drawComponent(Component* c)
{
    double x = DIE_START + ((double) c->getX()* DIE_SCALE  / (double) m_die_width );
    double y = DIE_START + ((double) c->getY()* DIE_SCALE  / (double) m_die_height);
    double width = max<double>(MIN_SIZE, c->getXsize()* DIE_SCALE  / (double) m_die_width);
    double height =max<double>(MIN_SIZE, c->getYsize()* DIE_SCALE  / (double) m_die_height);
    cairo_rectangle (m_cairo_ctx, x, y, width, height);
}

void Visualizer::drawIOPad(IOPad* p)
{
    double start_x = (double) p->getX() ;
    double start_y = (double) p->getY() ;
    double x = DIE_START + (start_x / (double) m_die_width ) * DIE_SCALE;
    double y = DIE_START + (start_y / (double) m_die_height) * DIE_SCALE;
    double width = max<double>(MIN_SIZE, (p->getXsize() / (double) m_die_width) * DIE_SCALE);
    double height =max<double>(MIN_SIZE, (p->getYsize() / (double) m_die_height) * DIE_SCALE);
    cairo_rectangle (m_cairo_ctx, x, y, width, height);
}

void Visualizer::highlightNet(Net* net_p)
{
    Box bb = net_p->getBoundingBox();
    // draw a rect around the net
    cairo_set_source_rgb (m_cairo_ctx, 1.0, 1.0, 0.0); // bright yellow
    cairo_set_line_width (m_cairo_ctx, 0.002);
    cairo_rectangle (m_cairo_ctx, DIE_START + bb.getPosBottomLeft().x * DIE_SCALE / m_die_width, // x
                         DIE_START + bb.getPosBottomLeft().y * DIE_SCALE / m_die_height,// y
                         bb.getXsize() * DIE_SCALE / m_die_width,   // width
                         bb.getYsize() * DIE_SCALE / m_die_height); // height
    cairo_stroke(m_cairo_ctx);

    // draw X's for each pin location (node + offset)
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black
    float sum_X = 0;
    float sum_Y = 0;
    for(const NetPin& pin : net_p->getPins()) {
        Position p = pin.getPos();
        sum_X += p.x;
        sum_Y += p.y;
        drawCross(DIE_START + p.x * DIE_SCALE / m_die_width,
              DIE_START + p.y * DIE_SCALE / m_die_height);
    }
    cairo_stroke(m_cairo_ctx);


    // draw rat's nest of connecting wires!
    float avg_X = DIE_START + sum_X / net_p->getPins().size() * DIE_SCALE / m_die_width;
    float avg_Y = DIE_START + sum_Y / net_p->getPins().size() * DIE_SCALE / m_die_height;
    cairo_set_line_width (m_cairo_ctx, 0.0004); // very thin lines for rat's nest
    for(const NetPin& pin : net_p->getPins()) {
        Position p = pin.getPos();
        cairo_move_to(m_cairo_ctx, DIE_START + p.x * DIE_SCALE / m_die_width,
                            DIE_START + p.y * DIE_SCALE / m_die_height);
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
    float node_X = DIE_START + node_p->getX() * DIE_SCALE / m_die_width;
    float node_Y = DIE_START + node_p->getY() * DIE_SCALE / m_die_height;
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

    // Draw die boundary in black
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0);
    cairo_set_line_width (m_cairo_ctx, 0.004);
    cairo_rectangle (m_cairo_ctx, DIE_START, DIE_START, DIE_SCALE, DIE_SCALE);
    cairo_stroke(m_cairo_ctx);

    drawFillerCells(db);
    drawFixedComponents(db);

    // Movable cells split by kind: std cells blue, movable macros red. Classification is
    // Node::isMovableMacro() — the design's single macro definition (Placer::tagMovableMacros,
    // XPlace is_mov_macro) — rather than a die-area threshold recomputed here, so the picture
    // shows the same macro set the placer and the filler math act on.
    drawMovableStandardCells(db);
    drawMovableMacros(db);

    drawAllIOPads(db);
    drawFocusHighlights(db);

    // draw reticle in center
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black
    drawReticle(0.5, 0.5);

    drawPlacementInfoOverlay(info);
    exportPlacementPNG(dir, info);
}

void Visualizer::drawFillerCells(DataBase& db)
{
    for (const auto& item : db.getFillers()) {
       drawComponent(item);
    }
    cairo_set_source_rgb (m_cairo_ctx, 0.9, 0.9, 0.9);  // grey
    cairo_fill(m_cairo_ctx);
}

void Visualizer::drawFixedComponents(DataBase& db)
{
    for (const auto& item : db.getComponents()) {
       if (item.second->getStatus() == FIXED)
           drawComponent(item.second);
    }
    cairo_set_source_rgb (m_cairo_ctx, 0.8, 0.0, 0.0); // red for fixed components
    cairo_fill_preserve(m_cairo_ctx);
    cairo_set_source_rgb (m_cairo_ctx, 0.0, 0.0, 0.0); // black border
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
    cairo_fill(m_cairo_ctx);
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
            drawReticle(scale(float(j)/bpr), scale(float(i)/bpc), 0.004);
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
            drawArrow(scale((j+.5f)/bpr), scale((i+.5f)/bpc), x_mag, y_mag);
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