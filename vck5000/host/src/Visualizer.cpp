#include "Visualizer.h"
#include "Logger.h"

#ifdef CREATE_VISUALIZATION

AIEPLACE_NAMESPACE_BEGIN 

void Visualizer::init(Box<position_type> die_area)
{
    m_die_area = die_area;
    m_die_width = die_area.getXsize();
    m_die_height= die_area.getYsize();

    m_canvas_width = m_die_width * 1.2;
    m_canvas_height = m_die_height * 1.2;

    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, CANVAS_PIXELS, CANVAS_PIXELS);
    cr = cairo_create (surface);
	cairo_scale (cr, CANVAS_PIXELS, CANVAS_PIXELS);
}


void Visualizer::drawComponent(Component* c)
{
    double x = DIE_START + ((double) c->getX()* DIE_SCALE  / (double) m_die_width );
    double y = DIE_START + ((double) c->getY()* DIE_SCALE  / (double) m_die_height);
    double width = max<double>(MIN_SIZE, c->getXsize()* DIE_SCALE  / (double) m_die_width);
    double height =max<double>(MIN_SIZE, c->getYsize()* DIE_SCALE  / (double) m_die_height);
    cairo_rectangle (cr, x, y, width, height);
}

void Visualizer::drawPin(Pin* p)
{
    double x = DIE_START + ((double) p->getX() / (double) m_die_width ) * DIE_SCALE;
    double y = DIE_START + ((double) p->getY() / (double) m_die_height) * DIE_SCALE;
    double width = max<double>(MIN_SIZE, (p->getXsize() / (double) m_die_width) * DIE_SCALE);
    double height =max<double>(MIN_SIZE, (p->getYsize() / (double) m_die_height) * DIE_SCALE);
    cairo_rectangle (cr, x, y, width, height);
}

void Visualizer::highlightNet(Net* net)
{
    Box<float> bb = net->getBoundingBox();
    // draw a rect around the net
    cairo_set_source_rgb (cr, 0.2, 1.0, 0.0); // bright green
    cairo_set_line_width (cr, 0.004);
    cairo_rectangle (cr, DIE_START + bb.getPosBottomLeft().getX() * DIE_SCALE / m_die_width, // x
                         DIE_START + bb.getPosBottomLeft().getY() * DIE_SCALE / m_die_height,// y
                         bb.getXsize() * DIE_SCALE / m_die_width,   // width
                         bb.getYsize() * DIE_SCALE / m_die_height); // height
    cairo_stroke(cr);
}

void  Visualizer::highlightNode(Node* node)
{
    for(Net* net : node->getNets())
        highlightNet(net);

    drawCross(DIE_START + node->getX() * DIE_SCALE / m_die_width,
              DIE_START + node->getY() * DIE_SCALE / m_die_height);
}

void Visualizer::drawCross(float x, float y)
{
    // draw small cross at (x, y)
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0); // black
    cairo_set_line_width (cr, 0.002);
    float cross_size = 0.008;
    cairo_move_to(cr, x - cross_size, y - cross_size);  
    cairo_line_to(cr, x + cross_size, y + cross_size);  
    cairo_move_to(cr, x + cross_size, y - cross_size);  
    cairo_line_to(cr, x - cross_size, y + cross_size);  
    cairo_stroke(cr);
}

void Visualizer::drawPlacement(DataBase db, fs::path dir, int iteration)
{
    // Start with a white background
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0); // white
    cairo_paint(cr);

    // Draw die boundary in black
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_set_line_width (cr, 0.004);
    cairo_rectangle (cr, DIE_START, DIE_START, 1-2*DIE_START, 1-2*DIE_START);
    cairo_stroke(cr);
    
    // Draw Components
    for (auto item : db.getComponents())
       drawComponent(item.second);
    cairo_set_source_rgb (cr, 0.0, 0.0, 1.0); // blue
    cairo_fill(cr);

    // Draw Pins
    for (auto item : db.getPins())
       drawPin(item.second);
    cairo_set_source_rgb (cr, 0.0, 1.0, 0.0); // green
    cairo_fill(cr);

    // Highlight Focus Nets
    for(Net* net : db.getFocusNets()) 
        highlightNet(net);

    // Highlight Focus Nodes
    for(Node* node : db.getFocusNodes()) 
        highlightNode(node);

    // draw cross hair in center
    float cross_size = 0.008;
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0); // black
    cairo_set_line_width (cr, 0.002);
    cairo_move_to(cr, 0.5 - cross_size, 0.5);
    cairo_line_to(cr, 0.5 + cross_size, 0.5);
    cairo_move_to(cr, 0.5, 0.5 - cross_size);
    cairo_line_to(cr, 0.5, 0.5 + cross_size);
    cairo_stroke(cr);

    // export image
    // index the image based on iteration
    string filename = "placement_";
    filename.append(std::to_string(iteration));
    filename.append(".png");
    dir.append(filename);
    Table t;
    t.add_row(RowStream{} << "VISUALIZER output PNG to " << dir);
    log("INFO", t);
    cairo_surface_write_to_png (surface, dir.c_str());
}

void Visualizer::drawElectricField(DataBase db, fs::path dir, int iteration)
{
    // Draw bins

    // Draw arrows

    // Export image


}

AIEPLACE_NAMESPACE_END

#endif