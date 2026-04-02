#include "Visualizer.h"

#ifdef CREATE_VISUALIZATION

AIEPLACE_NAMESPACE_BEGIN 

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

    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, m_canvas_px_w, m_canvas_px_h);
    cr = cairo_create (surface);
    cairo_scale (cr, m_canvas_px_w, m_canvas_px_h);
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
    cairo_rectangle (cr, x, y, width, height);
}

void Visualizer::drawIOPad(IOPad* p)
{
    double start_x = (double) p->getX() ;
    double start_y = (double) p->getY() ;
    double x = DIE_START + (start_x / (double) m_die_width ) * DIE_SCALE;
    double y = DIE_START + (start_y / (double) m_die_height) * DIE_SCALE;
    double width = max<double>(MIN_SIZE, (p->getXsize() / (double) m_die_width) * DIE_SCALE);
    double height =max<double>(MIN_SIZE, (p->getYsize() / (double) m_die_height) * DIE_SCALE);
    //cairo_rectangle (cr, x, y, width, height);
}

void Visualizer::highlightNet(Net* net)
{
    Box bb = net->getBoundingBox();
    // draw a rect around the net
    cairo_set_source_rgb (cr, 1.0, 1.0, 0.0); // bright yellow
    cairo_set_line_width (cr, 0.002);
    cairo_rectangle (cr, DIE_START + bb.getPosBottomLeft().x * DIE_SCALE / m_die_width, // x
                         DIE_START + bb.getPosBottomLeft().y * DIE_SCALE / m_die_height,// y
                         bb.getXsize() * DIE_SCALE / m_die_width,   // width
                         bb.getYsize() * DIE_SCALE / m_die_height); // height
    cairo_stroke(cr);

    // draw X's for each node location
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0); // black
    float sum_X = 0;
    float sum_Y = 0;
    for(Node* node: net->getNodes()) {
        sum_X += node->getX();
        sum_Y += node->getY();
        drawCross(DIE_START + node->getX() * DIE_SCALE / m_die_width,
              DIE_START + node->getY() * DIE_SCALE / m_die_height);
    }
    cairo_stroke(cr);


    // draw rat's nest of connecting wires!
    float avg_X = DIE_START + sum_X / net->getNodes().size() * DIE_SCALE / m_die_width;
    float avg_Y = DIE_START + sum_Y / net->getNodes().size() * DIE_SCALE / m_die_height;
    cairo_set_line_width (cr, 0.0004); // very thin lines for rat's nest
    for(Node* node: net->getNodes()) {
        cairo_move_to(cr, DIE_START + node->getX() * DIE_SCALE / m_die_width,
                            DIE_START + node->getY() * DIE_SCALE / m_die_height);
        cairo_line_to(cr, avg_X, avg_Y);  
    }
    cairo_stroke(cr);
}

void  Visualizer::highlightNode(Node* node)
{
    for(Net* net : node->getNets())
        highlightNet(net);

    // draw a red cross on the highlighted node
    cairo_set_source_rgb (cr, 1.0, 0.0, 0.0); // red 
    float node_X = DIE_START + node->getX() * DIE_SCALE / m_die_width;
    float node_Y = DIE_START + node->getY() * DIE_SCALE / m_die_height;
    drawCross(node_X, node_Y);
    cairo_arc(cr, node_X, node_Y, .002, 0, 2 * M_PI);
    cairo_stroke(cr);

    // Write the highlighted net's name in bottom left corner
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0); // black
    cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, .02);
    cairo_move_to (cr, .1, .99);
    std::string highlight_str = "Focus Node: " + node->getName();
    cairo_show_text (cr, highlight_str.c_str());
    cairo_move_to (cr, .6, .99);
    cairo_stroke(cr);
}

void Visualizer::drawCross(float x, float y, float cross_size)
{
    // draw small cross at (x, y)
    // assumes color has already been set!
    cairo_set_line_width (cr, 0.001);
    cairo_move_to(cr, x - cross_size, y - cross_size);  
    cairo_line_to(cr, x + cross_size, y + cross_size);  
    cairo_move_to(cr, x + cross_size, y - cross_size);  
    cairo_line_to(cr, x - cross_size, y + cross_size);  
    cairo_stroke(cr);
}

void Visualizer::drawReticle(float x, float y, float reticle_size)
{
    cairo_set_line_width (cr, 0.001);
    cairo_move_to(cr, x - reticle_size, y);
    cairo_line_to(cr, x + reticle_size, y);
    cairo_move_to(cr, x, y - reticle_size);
    cairo_line_to(cr, x, y + reticle_size);
    cairo_stroke(cr);
}

void Visualizer::drawArrow(float x, float y, float x_mag, float y_mag)
{
    cairo_set_line_width (cr, 0.001);
    // draw body of arrow
    cairo_move_to(cr, x - x_mag, y - y_mag);  
    cairo_line_to(cr, x + x_mag, y + y_mag);  
    cairo_stroke(cr);
    
    // draw head of arrow
    cairo_arc(cr, x + x_mag, y + y_mag, .002, 0, 2 * M_PI);
    cairo_fill(cr);

}

void Visualizer::drawPlacement(DataBase& db, fs::path dir, PlotInfo info)
{
    Logger::log_info("Exporting placement visualization to PNG...");
    // Draw items from back to front in order of Fillers, Components, Pins, Nets, Focus Nets, Focus Nodes
    // Start with a white background
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0); // white
    cairo_paint(cr);

    // Draw die boundary in black
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_set_line_width (cr, 0.004);
    cairo_rectangle (cr, DIE_START, DIE_START, DIE_SCALE, DIE_SCALE);
    cairo_stroke(cr);
    
    // Draw Fillers
    for (auto item : db.getFillers()) {
       drawComponent(item);
    }
    cairo_set_source_rgb (cr, 0.9, 0.9, 0.9);  // grey 
    cairo_fill(cr);

    // Draw Fixed Components 
    for (auto item : db.getComponents()) {
       if (item.second->getStatus() == FIXED)
           drawComponent(item.second);
    }
    cairo_set_source_rgb (cr, 0.8, 0.0, 0.0); // red for fixed components
    cairo_fill_preserve(cr);
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0); // black border
    cairo_set_line_width (cr, 0.001);
    cairo_stroke(cr);

    // Draw Movable Components (blue)
    for (auto item : db.getComponents()) {
       if (item.second->getStatus() != FIXED)
           drawComponent(item.second);
    }
    cairo_set_source_rgb (cr, 0.0, 0.0, 1.0); // blue
    cairo_fill(cr);

    // Draw IO Pads
    for (auto item : db.getIOPads())
        drawIOPad(item.second);

    cairo_set_source_rgb (cr, 1.0, 0.64, 0.0); // orange 
    cairo_fill(cr);

    // Highlight Focus Nets
    for(Net* net : db.getFocusNets()) 
        highlightNet(net);

    // Highlight Focus Nodes
    for(Node* node : db.getFocusNodes()) 
        highlightNode(node);

    // draw reticle in center
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0); // black
    drawReticle(0.5, 0.5);

    // print current iteration and other info at bottom of image
    cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size (cr, .02);

    cairo_move_to (cr, .01, .04);
    std::string bench_str = "Benchmark: " + info.benchmark_name; 
    cairo_show_text (cr, bench_str.c_str());

    cairo_move_to (cr, .01, .99);
    std::string iter_str = "Iter: " + std::to_string(info.iteration);
    cairo_show_text (cr, iter_str.c_str());

    cairo_move_to (cr, .18, .99);
    std::string hpwl_str = "HPWL: " + SCI(info.hpwl);
    cairo_show_text (cr, hpwl_str.c_str());

    cairo_move_to (cr, .40, .99);
    std::string ovfw_str = "OVFW: " + PREC_P(info.overflow, 2);
    cairo_show_text (cr, ovfw_str.c_str());

    if (info.filename_override.empty()) {
        cairo_move_to (cr, .60, .99);
        std::string alpha_str = "alpha: " + SCI(info.step_length);
        cairo_show_text (cr, alpha_str.c_str());

        cairo_move_to (cr, .78, .99);
        std::string lambda_str = "lambda: " + SCI(info.density_weight);
        cairo_show_text (cr, lambda_str.c_str());
    }

    cairo_stroke(cr);


    // export image
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
    Logger::log_info(t);
    cairo_surface_write_to_png (surface, dir.c_str());
}

void Visualizer::drawElectricField(Grid& grid, fs::path dir, int iteration)
{
    // Start with a white background
    //cairo_set_source_rgb (cr, 1.0, 1.0, 1.0); // white
    //cairo_paint(cr);

    // Draw bin reticles
    int bpr = grid.getBinsPerRow();
    int bpc = grid.getBinsPerCol();
    cairo_set_source_rgb (cr, 0.75, 0.75, 0.75); // grey
    for(int i = 1; i < bpc; i++) {
        for(int j = 1; j < bpr; j++) {
            drawReticle(scale(float(j)/bpr), scale(float(i)/bpc), 0.004);
        }
    }

    // Draw arrows in middle of bins
    cairo_set_source_rgb (cr, 0.05, 0.05, 0.05); // black
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
    Logger::log_info(t);
    cairo_surface_write_to_png (surface, dir.c_str());

}

AIEPLACE_NAMESPACE_END

#endif