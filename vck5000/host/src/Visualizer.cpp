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
    double x = DIE_START + ((double) c->getX() / (double) m_die_width ) * (1- 2*DIE_START);
    double y = DIE_START + ((double) c->getY() / (double) m_die_height) * (1- 2*DIE_START);
    double width = max<double>(MIN_SIZE, c->getXsize() / (double) m_die_width);
    double height =max<double>(MIN_SIZE, c->getYsize() / (double) m_die_height);
    cairo_rectangle (cr, x, y, width, height);
}

void Visualizer::drawPin(Pin* p)
{
    double x = DIE_START + ((double) p->getX() / (double) m_die_width ) * (1- 2*DIE_START);
    double y = DIE_START + ((double) p->getY() / (double) m_die_height) * (1- 2*DIE_START);
    double width = max<double>(MIN_SIZE, p->getXsize() / (double) m_die_width);
    double height =max<double>(MIN_SIZE, p->getYsize() / (double) m_die_height);
    cairo_rectangle (cr, x, y, width, height);
}

void Visualizer::highlightNet(Net* net)
{

}

void Visualizer::drawPlacement(DataBase db, fs::path dir, int iteration)
{
    // Start with a white background
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);

    // Draw die boundary in black
    cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
    cairo_set_line_width (cr, 0.004);
    cairo_rectangle (cr, DIE_START, DIE_START, 1-2*DIE_START, 1-2*DIE_START);
    cairo_stroke(cr);

    // Draw bins

    // Draw Components
    for (auto item : db.getComponents())
       drawComponent(item.second);
    cairo_set_source_rgb (cr, 1.0, 0.0, 0.0); // red
    cairo_fill(cr);

    // Draw Pins
    for (auto item : db.getPins())
       drawPin(item.second);
    cairo_set_source_rgb (cr, 0.0, 1.0, 0.0); // green
    cairo_fill(cr);


    // export image
    // index the image based on iteration
    string filename = "placement_";
    filename.append(std::to_string(iteration));
    filename.append(".png");
    dir.append(filename);
    Table t;
    t.add_row(RowStream{} << "VISUALIZER: PNG output to " << dir);
    log("INFO", t);
    cairo_surface_write_to_png (surface, dir.c_str());
}

AIEPLACE_NAMESPACE_END

#endif