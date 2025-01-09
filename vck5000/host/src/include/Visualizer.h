#ifndef VISUALIZER_H
#define VISUALIZER_H

#include "Common.h"

#ifdef CREATE_VISUALIZATION
#include <cairo/cairo.h>
#include "DataBase.h"
#include <chrono> // for naming output directories by the minute

AIEPLACE_NAMESPACE_BEGIN 

class Visualizer
{
    private:

    // Member Data
    Box<position_type> m_die_area;
    double m_die_width, m_die_height;
    double m_canvas_width, m_canvas_height;

    const int CANVAS_PIXELS = 1048;
    double DIE_START = 0.05; // boundary 
    double MIN_SIZE = 0.004; // Minimum size to be visible
    cairo_surface_t *surface;
    cairo_t *cr;

    public:

    // Constructor
    Visualizer(Box<position_type> die_area);

    void drawComponent(Component* c);
    void drawPin(Pin* p);
    void highlightNet(Net* net);
    void drawPlacement(DataBase, fs::path, int);
};

AIEPLACE_NAMESPACE_END

#endif
#endif