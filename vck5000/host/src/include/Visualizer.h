#ifndef VISUALIZER_H
#define VISUALIZER_H

#include "Common.h"

#ifdef CREATE_VISUALIZATION
#include <cairo/cairo.h>
#include "DataBase.h"
#include "Grid.h"
#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN 

class Visualizer
{
    private:

    // Member Data
    Box<position_type> m_die_area;
    float m_die_width, m_die_height;
    float m_canvas_width, m_canvas_height;

    const int CANVAS_PIXELS = 2048;//1024; // reasonable trade off between image size and detail shown.
    const float DIE_START = 0.05; // boundary 
    const float DIE_SCALE = 1 - 2*DIE_START; // scale for drawing components on the die
    const float MIN_SIZE = 0.001; // Minimum size to be visible
    cairo_surface_t *surface;
    cairo_t *cr;

    public:

    // Constructor
    Visualizer() {};
    
    void init(Box<position_type> die_area);
    float scale(float f);
    void drawComponent(Component* c);
    void drawPin(Pin* p);
    void highlightNet(Net* net);
    void highlightNode(Node* node);
    void drawCross(float x, float y, float cross_size = 0.004);
    void drawReticle(float x, float y, float reticle_size = 0.008);
    void drawArrow(float x, float y, float x_mag, float y_mag);
    void drawPlacement(DataBase&, fs::path, int);
    void drawElectricField(Grid&, fs::path, int);
};

AIEPLACE_NAMESPACE_END

#endif
#endif