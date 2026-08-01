/**
 * @file Visualizer.h
 * @brief Cairo-based renderer for placement snapshots and metric plots (PNG export).
 */
#pragma once

#include "Common.h"

#ifdef CREATE_VISUALIZATION
#include <cairo/cairo.h>
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
};

class Visualizer
{
    private:

    // Member Data
    Box m_die_area;
    float m_die_width, m_die_height;
    float m_canvas_width, m_canvas_height;

    const int MAX_CANVAS_PX = 2048; // longest dimension in pixels
    int m_canvas_px_w, m_canvas_px_h; // computed from die aspect ratio in init()
    const float DIE_SCALE = 0.80; // die occupies 80% of canvas
    const float DIE_START = (1 - DIE_SCALE) / 2; // 10% margin on all sides
    const float MIN_SIZE = 0.001; // Minimum size to be visible
    cairo_surface_t *m_surface;
    cairo_t *m_cairo_ctx;

    // drawPlacement()'s steps, broken out for readability
    void drawFillerCells(DataBase& db);
    void drawFixedComponents(DataBase& db);
    void drawMovableStandardCells(DataBase& db);
    void drawMovableMacros(DataBase& db);
    void drawAllIOPads(DataBase& db);
    void drawFocusHighlights(DataBase& db);
    void drawPlacementInfoOverlay(const PlotInfo& info);
    void exportPlacementPNG(fs::path dir, const PlotInfo& info);

    public:

    // Constructor
    Visualizer() {};

    void init(Box die_area);
    float scale(float f);
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

class CairoPlotter {
private:
    cairo_surface_t* m_surface;
    cairo_t* m_cairo_ctx;
    int width, height;
    int margin = 60;
    
    // Helper function to map data coordinates to screen coordinates
    double mapX(double value, double min_val, double max_val, double plot_width) {
        return margin + (value - min_val) / (max_val - min_val) * plot_width;
    }
    
    double mapY(double value, double min_val, double max_val, double plot_height) {
        return height - margin - (value - min_val) / (max_val - min_val) * plot_height;
    }
    
    void drawAxes(double x_min, double x_max, double y_min, double y_max, 
                  const std::string& x_label, const std::string& y_label) {
        double plot_width = width - 2 * margin;
        double plot_height = height - 2 * margin;
        
        // Set line properties
        cairo_set_line_width(m_cairo_ctx, 2.0);
        cairo_set_source_rgb(m_cairo_ctx, 0.2, 0.2, 0.2);
        
        // Draw X axis
        cairo_move_to(m_cairo_ctx, margin, height - margin);
        cairo_line_to(m_cairo_ctx, width - margin, height - margin);
        cairo_stroke(m_cairo_ctx);
        
        // Draw Y axis
        cairo_move_to(m_cairo_ctx, margin, margin);
        cairo_line_to(m_cairo_ctx, margin, height - margin);
        cairo_stroke(m_cairo_ctx);
        
        // Add grid lines
        cairo_set_line_width(m_cairo_ctx, 0.5);
        cairo_set_source_rgb(m_cairo_ctx, 0.8, 0.8, 0.8);
        
        // Vertical grid lines
        for (int i = 1; i < 10; i++) {
            double x = margin + i * plot_width / 10;
            cairo_move_to(m_cairo_ctx, x, margin);
            cairo_line_to(m_cairo_ctx, x, height - margin);
            cairo_stroke(m_cairo_ctx);
        }
        
        // Horizontal grid lines
        for (int i = 1; i < 10; i++) {
            double y = margin + i * plot_height / 10;
            cairo_move_to(m_cairo_ctx, margin, y);
            cairo_line_to(m_cairo_ctx, width - margin, y);
            cairo_stroke(m_cairo_ctx);
        }
        
        // Add labels
        cairo_set_source_rgb(m_cairo_ctx, 0, 0, 0);
        cairo_select_font_face(m_cairo_ctx, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(m_cairo_ctx, 14);
        
        // X-axis label
        cairo_text_extents_t extents;
        cairo_text_extents(m_cairo_ctx, x_label.c_str(), &extents);
        cairo_move_to(m_cairo_ctx, (width - extents.width) / 2, height - 10);
        cairo_show_text(m_cairo_ctx, x_label.c_str());
        
        // Y-axis label (rotated)
        cairo_save(m_cairo_ctx);
        cairo_translate(m_cairo_ctx, 15, height / 2);
        cairo_rotate(m_cairo_ctx, -M_PI / 2);
        cairo_text_extents(m_cairo_ctx, y_label.c_str(), &extents);
        cairo_move_to(m_cairo_ctx, -extents.width / 2, 0);
        cairo_show_text(m_cairo_ctx, y_label.c_str());
        cairo_restore(m_cairo_ctx);
    }
    
    void drawTicks(double x_min, double x_max, double y_min, double y_max) {
        cairo_set_source_rgb(m_cairo_ctx, 0, 0, 0);
        cairo_set_font_size(m_cairo_ctx, 10);
        cairo_set_line_width(m_cairo_ctx, 1.0);
        
        double plot_width = width - 2 * margin;
        double plot_height = height - 2 * margin;
        
        // X-axis ticks and labels
        for (int i = 0; i <= 10; i++) {
            double x_val = x_min + i * (x_max - x_min) / 10;
            double x_pos = margin + i * plot_width / 10;
            
            // Tick mark
            cairo_move_to(m_cairo_ctx, x_pos, height - margin);
            cairo_line_to(m_cairo_ctx, x_pos, height - margin + 5);
            cairo_stroke(m_cairo_ctx);
            
            // Label
            std::string label = std::to_string((int)x_val);
            cairo_text_extents_t extents;
            cairo_text_extents(m_cairo_ctx, label.c_str(), &extents);
            cairo_move_to(m_cairo_ctx, x_pos - extents.width / 2, height - margin + 20);
            cairo_show_text(m_cairo_ctx, label.c_str());
        }
        
        // Y-axis ticks and labels
        for (int i = 0; i <= 10; i++) {
            double y_val = y_min + i * (y_max - y_min) / 10;
            double y_pos = height - margin - i * plot_height / 10;
            
            // Tick mark
            cairo_move_to(m_cairo_ctx, margin - 5, y_pos);
            cairo_line_to(m_cairo_ctx, margin, y_pos);
            cairo_stroke(m_cairo_ctx);
            
            // Label
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%.2e", y_val);
            cairo_text_extents_t extents;
            cairo_text_extents(m_cairo_ctx, buffer, &extents);
            cairo_move_to(m_cairo_ctx, margin - extents.width - 10, y_pos + extents.height / 2);
            cairo_show_text(m_cairo_ctx, buffer);
        }
    }

public:
    CairoPlotter(int w, int h) : width(w), height(h) {
        m_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        m_cairo_ctx = cairo_create(m_surface);
        
        // Fill background with white
        cairo_set_source_rgb(m_cairo_ctx, 1, 1, 1);
        cairo_paint(m_cairo_ctx);
    }
    
    ~CairoPlotter() {
        cairo_destroy(m_cairo_ctx);
        cairo_surface_destroy(m_surface);
    }
    
    void plotHistory(const std::vector<float>& data, 
                    const std::string& title,
                    const std::string& y_label,
                    double r = 0.0, double g = 0.5, double b = 1.0) {
        if (data.empty()) return;
        
        // Find data bounds
        auto minmax_y = std::minmax_element(data.begin(), data.end());
        double y_min = *minmax_y.first;
        double y_max = *minmax_y.second;
        double x_min = 0;
        double x_max = data.size() - 1;
        
        // Add some padding to y-axis
        double y_range = y_max - y_min;
        y_min -= y_range * 0.1;
        y_max += y_range * 0.1;
        
        double plot_width = width - 2 * margin;
        double plot_height = height - 2 * margin;
        
        // Draw axes and grid
        drawAxes(x_min, x_max, y_min, y_max, "Iteration", y_label);
        drawTicks(x_min, x_max, y_min, y_max);
        
        // Plot the data
        cairo_set_source_rgb(m_cairo_ctx, r, g, b);
        cairo_set_line_width(m_cairo_ctx, 2.0);
        
        // Draw the line
        bool first_point = true;
        for (size_t i = 0; i < data.size(); ++i) {
            double x = mapX(i, x_min, x_max, plot_width);
            double y = mapY(data[i], y_min, y_max, plot_height);
            
            if (first_point) {
                cairo_move_to(m_cairo_ctx, x, y);
                first_point = false;
            } else {
                cairo_line_to(m_cairo_ctx, x, y);
            }
        }
        cairo_stroke(m_cairo_ctx);
        
        // Draw points
        cairo_set_source_rgb(m_cairo_ctx, r * 0.8, g * 0.8, b * 0.8);
        for (size_t i = 0; i < data.size(); ++i) {
            double x = mapX(i, x_min, x_max, plot_width);
            double y = mapY(data[i], y_min, y_max, plot_height);
            
            cairo_arc(m_cairo_ctx, x, y, 3, 0, 2 * M_PI);
            cairo_fill(m_cairo_ctx);
        }
        
        // Add title
        cairo_set_source_rgb(m_cairo_ctx, 0, 0, 0);
        cairo_select_font_face(m_cairo_ctx, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(m_cairo_ctx, 16);
        cairo_text_extents_t extents;
        cairo_text_extents(m_cairo_ctx, title.c_str(), &extents);
        cairo_move_to(m_cairo_ctx, (width - extents.width) / 2, 30);
        cairo_show_text(m_cairo_ctx, title.c_str());
    }
    
    void plotDualHistory(const std::vector<float>& data1, const std::vector<float>& data2,
                        const std::string& title,
                        const std::string& label1, const std::string& label2) {
        if (data1.empty() && data2.empty()) return;
        
        // Normalize both datasets to [0, 1] for dual-axis plotting
        auto minmax1 = std::minmax_element(data1.begin(), data1.end());
        auto minmax2 = std::minmax_element(data2.begin(), data2.end());
        
        double range1 = *minmax1.second - *minmax1.first;
        double range2 = *minmax2.second - *minmax2.first;
        
        size_t max_size = std::max(data1.size(), data2.size());
        double x_min = 0, x_max = max_size - 1;
        double y_min = 0, y_max = 1;
        
        double plot_width = width - 2 * margin;
        double plot_height = height - 2 * margin;
        
        // Draw basic axes
        drawAxes(x_min, x_max, y_min, y_max, "Iteration", "Normalized Values");
        
        // Plot first dataset (HPWL) in blue
        cairo_set_source_rgb(m_cairo_ctx, 0.0, 0.5, 1.0);
        cairo_set_line_width(m_cairo_ctx, 2.0);
        
        bool first_point = true;
        for (size_t i = 0; i < data1.size(); ++i) {
            double normalized_val = (data1[i] - *minmax1.first) / range1;
            double x = mapX(i, x_min, x_max, plot_width);
            double y = mapY(normalized_val, y_min, y_max, plot_height);
            
            if (first_point) {
                cairo_move_to(m_cairo_ctx, x, y);
                first_point = false;
            } else {
                cairo_line_to(m_cairo_ctx, x, y);
            }
        }
        cairo_stroke(m_cairo_ctx);
        
        // Plot second dataset (Learning Coeff) in red
        cairo_set_source_rgb(m_cairo_ctx, 1.0, 0.2, 0.2);
        cairo_set_line_width(m_cairo_ctx, 2.0);
        
        first_point = true;
        for (size_t i = 0; i < data2.size(); ++i) {
            double normalized_val = (data2[i] - *minmax2.first) / range2;
            double x = mapX(i, x_min, x_max, plot_width);
            double y = mapY(normalized_val, y_min, y_max, plot_height);
            
            if (first_point) {
                cairo_move_to(m_cairo_ctx, x, y);
                first_point = false;
            } else {
                cairo_line_to(m_cairo_ctx, x, y);
            }
        }
        cairo_stroke(m_cairo_ctx);
        
        // Add legend
        cairo_set_font_size(m_cairo_ctx, 12);
        
        // HPWL legend
        cairo_set_source_rgb(m_cairo_ctx, 0.0, 0.5, 1.0);
        cairo_rectangle(m_cairo_ctx, width - 150, 50, 20, 10);
        cairo_fill(m_cairo_ctx);
        cairo_set_source_rgb(m_cairo_ctx, 0, 0, 0);
        cairo_move_to(m_cairo_ctx, width - 125, 60);
        cairo_show_text(m_cairo_ctx, label1.c_str());
        
        // Learning Coeff legend
        cairo_set_source_rgb(m_cairo_ctx, 1.0, 0.2, 0.2);
        cairo_rectangle(m_cairo_ctx, width - 150, 70, 20, 10);
        cairo_fill(m_cairo_ctx);
        cairo_set_source_rgb(m_cairo_ctx, 0, 0, 0);
        cairo_move_to(m_cairo_ctx, width - 125, 80);
        cairo_show_text(m_cairo_ctx, label2.c_str());
        
        // Add title
        cairo_set_source_rgb(m_cairo_ctx, 0, 0, 0);
        cairo_select_font_face(m_cairo_ctx, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(m_cairo_ctx, 16);
        cairo_text_extents_t extents;
        cairo_text_extents(m_cairo_ctx, title.c_str(), &extents);
        cairo_move_to(m_cairo_ctx, (width - extents.width) / 2, 30);
        cairo_show_text(m_cairo_ctx, title.c_str());
    }
    
    void savePNG(const std::string& filename) {
        cairo_surface_write_to_png(m_surface, filename.c_str());
    }
};


AIEPLACE_NAMESPACE_END

#endif
