##
# @file   PlaceDrawer.py
# @author Yibo Lin
# @date   Mar 2019
# @brief  A python implementation of placement drawer as an alternative when cairo C/C++ API is not available.
#

import sys
import os
import time
import math
import cairocffi as cairo
import numpy as np


class PlaceDrawer(object):
    """
    @brief A python implementation of placement drawer as an alternative when cairo C/C++ API is not available. 
    """
    @staticmethod
    def forward(pos,
                node_size_x,
                node_size_y,
                pin_offset_x,
                pin_offset_y,
                pin2node_map,
                xl,
                yl,
                xh,
                yh,
                site_width,
                row_height,
                bin_size_x,
                bin_size_y,
                num_movable_nodes,
                num_filler_nodes,
                filename,
                nets=[],
                hwpl_force=[],
                bin_force_x=[],
                bin_force_y=[],
                density_penalty=0,
                iteration=None):
        """
        @brief python implementation of placement drawer.  
        @param pos locations of cells 
        @param node_size_x array of cell width 
        @param node_size_y array of cell height 
        @param pin_offset_x pin offset to cell origin 
        @param pin_offset_y pin offset to cell origin 
        @param pin2node_map map pin to cell 
        @param xl left boundary 
        @param yl bottom boundary 
        @param xh right boundary 
        @param yh top boundary 
        @param site_width width of placement site 
        @param row_height height of placement row, equivalent to height of placement site 
        @param bin_size_x bin width 
        @param bin_size_y bin height 
        @param num_movable_nodes number of movable cells 
        @param num_filler_nodes number of filler cells 
        @param filename output filename 
        @param iteration current optimization step 
        """
        num_nodes = len(pos) // 2
        num_movable_nodes = num_movable_nodes
        num_filler_nodes = num_filler_nodes
        num_physical_nodes = num_nodes - num_filler_nodes
        num_bins_x = int(math.ceil((xh - xl) / bin_size_x))
        num_bins_y = int(math.ceil((yh - yl) / bin_size_y))
        x = np.array(pos[:num_nodes])
        y = np.array(pos[num_nodes:])
        node_size_x = np.array(node_size_x)
        node_size_y = np.array(node_size_y)
        pin_offset_x = np.array(pin_offset_x)
        pin_offset_y = np.array(pin_offset_y)
        pin2node_map = np.array(pin2node_map)
        try:
            tt = time.time()
            if xh - xl < yh - yl:
                height = 800 
                width = round(height * (xh - xl) / (yh - yl))
            else:
                width = 800 
                height = round(width * (yh - yl) / (xh -xl))
            line_width = 1 # make lines visible!
            padding = 0
            surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
            ctx = cairo.Context(surface)
            # Do not use scale function.
            # This is not compatible with show_text

            if num_movable_nodes < num_physical_nodes:
                layout_xl = min(
                    np.amin(x[num_movable_nodes:num_physical_nodes]), xl)
                layout_yl = min(
                    np.amin(y[num_movable_nodes:num_physical_nodes]), yl)
                layout_xh = max(
                    np.amax(x[num_movable_nodes:num_physical_nodes] +
                            node_size_x[num_movable_nodes:num_physical_nodes]),
                    xh)
                layout_yh = max(
                    np.amax(y[num_movable_nodes:num_physical_nodes] +
                            node_size_y[num_movable_nodes:num_physical_nodes]),
                    yh)
            else:
                layout_xl = xl
                layout_yl = yl
                layout_xh = xh
                layout_yh = yh

            def bin_xl(id_x):
                """
                @param id_x horizontal index 
                @return bin xl
                """
                return xl + id_x * bin_size_x

            def bin_xh(id_x):
                """
                @param id_x horizontal index 
                @return bin xh
                """
                return min(bin_xl(id_x) + bin_size_x, xh)

            def bin_yl(id_y):
                """
                @param id_y vertical index 
                @return bin yl
                """
                return yl + id_y * bin_size_y

            def bin_yh(id_y):
                """
                @param id_y vertical index 
                @return bin yh
                """
                return min(bin_yl(id_y) + bin_size_y, yh)

            def normalize_x(xx):
                return (xx - (layout_xl - padding * bin_size_x)) / (
                    layout_xh - layout_xl + padding * 2 * bin_size_x) * width

            def normalize_y(xx):
                return (xx - (layout_yl - padding * bin_size_y)) / (
                    layout_yh - layout_yl + padding * 2 * bin_size_y) * height

            def draw_rect(x1, y1, x2, y2):
                ctx.move_to(x1, y1)
                ctx.line_to(x1, y2)
                ctx.line_to(x2, y2)
                ctx.line_to(x2, y1)
                ctx.close_path()
                ctx.stroke()

            def draw_arrow(x1, y1, arrow_length, arrow_angle):
                arrowhead_angle = math.pi/6
                arrowhead_length = arrow_length/3

                ctx.move_to(x1, y1) # move to start
                #ctx.show_text('({:.2f}, {:.2f})'.format(dx, dy))
                ctx.move_to(x1, y1) # move to start
                ctx.rel_line_to(arrow_length * math.cos(arrow_angle), arrow_length * math.sin(arrow_angle))
                ctx.rel_move_to(-arrowhead_length * math.cos(arrow_angle - arrowhead_angle), -arrowhead_length * math.sin(arrow_angle - arrowhead_angle))
                ctx.rel_line_to(arrowhead_length * math.cos(arrow_angle - arrowhead_angle), arrowhead_length * math.sin(arrow_angle - arrowhead_angle))
                ctx.rel_line_to(-arrowhead_length * math.cos(arrow_angle + arrowhead_angle), -arrowhead_length * math.sin(arrow_angle + arrowhead_angle))
                ctx.set_source_rgb(0,0,0)
                ctx.set_line_width(0.05*arrow_length)
                ctx.stroke()

            # draw layout region
            ctx.set_source_rgb(1, 1, 1)
            draw_layout_xl = normalize_x(layout_xl - padding * bin_size_x)
            draw_layout_yl = normalize_y(layout_yl - padding * bin_size_y)
            draw_layout_xh = normalize_x(layout_xh + padding * bin_size_x)
            draw_layout_yh = normalize_y(layout_yh + padding * bin_size_y)
            ctx.rectangle(draw_layout_xl, draw_layout_yl, draw_layout_xh,
                          draw_layout_yh)
            ctx.fill()
            ctx.set_line_width(line_width)
            ctx.set_source_rgba(0.1, 0.1, 0.1, alpha=0.8)
            ctx.move_to(normalize_x(xl), normalize_y(yl))
            ctx.line_to(normalize_x(xl), normalize_y(yh))
            ctx.line_to(normalize_x(xh), normalize_y(yh))
            ctx.line_to(normalize_x(xh), normalize_y(yl))
            ctx.close_path()
            ctx.stroke()
            ## draw bins
            ctx.set_source_rgba(1, 0, 0, alpha=0.5)
            for i in range(1, num_bins_x):
                ctx.move_to(normalize_x(bin_xl(i)), normalize_y(yl))
                ctx.line_to(normalize_x(bin_xl(i)), normalize_y(yh))
                ctx.close_path()
                ctx.stroke()
            for i in range(1, num_bins_y):
                ctx.move_to(normalize_x(xl), normalize_y(bin_yl(i)))
                ctx.line_to(normalize_x(xh), normalize_y(bin_yl(i)))
                ctx.close_path()
                ctx.stroke()

            # draw selected nets
            ctx.set_line_width(line_width)
            ctx.set_source_rgba(0, 0, 0, alpha=0.5)
            for net in nets:
                maxx, maxy = 0, 0
                minx, miny = layout_xh, layout_yh
                for node_index in net:
                    if x[node_index] < minx: minx = x[node_index]
                    if x[node_index]+node_size_x[node_index] > maxx: maxx = x[node_index]+node_size_x[node_index]
                    if y[node_index] < miny: miny = y[node_index]
                    if y[node_index]+node_size_y[node_index] > maxy: maxy = y[node_index]+node_size_y[node_index]
                # Draw bounding box
                draw_rect(normalize_x(minx), normalize_x(miny), normalize_y(maxx), normalize_y(maxy))
                centerx = minx + (maxx - minx)/2
                centery = miny + (maxy - miny)/2
                
                # Draw connections
                for node_index in net:
                    ctx.move_to(normalize_x(x[node_index] + node_size_x[node_index]/2), normalize_y(y[node_index] + node_size_y[node_index]/2))
                    ctx.line_to(normalize_x(centerx), normalize_y(centery))
                    ctx.close_path()
                    ctx.stroke()

            # draw force arrows
            ctx.set_source_rgb(0, 0, 0)
            ctx.set_line_width(line_width * 10)
            ctx.select_font_face("monospace", cairo.FONT_SLANT_NORMAL,
                                    cairo.FONT_WEIGHT_NORMAL)
            ctx.set_font_size(6)
            for i in range(num_bins_y):
                for j in range(num_bins_x):
                    dx = 0.1*density_penalty*bin_force_y[i][j]
                    dy = 0.1*density_penalty*bin_force_x[i][j]
                    arrow_length = math.sqrt(dx**2 + dy**2)
                    arrow_angle = math.atan(dy/dx)
                    if dx < 0: # mirror across y axis
                        arrow_angle = math.pi - arrow_angle
                    draw_arrow(normalize_x(bin_xl(j) + bin_size_x/2), 
                               normalize_y(bin_yl(i) + bin_size_y/2),
                               normalize_x(arrow_length), arrow_angle)


            # draw cells
            ctx.set_font_size(16)
            ctx.select_font_face("monospace", cairo.FONT_SLANT_NORMAL,
                                 cairo.FONT_WEIGHT_NORMAL)
            node_xl = x
            node_yl = layout_yl + y # layout_yh - (y + node_size_y[0:len(y)]
            node_xh = node_xl + node_size_x[0:len(x)]
            node_yh = layout_yl + y + node_size_y[0:len(y)] # flip y
            node_xl = normalize_x(node_xl)
            node_yl = normalize_y(node_yl)
            node_xh = normalize_x(node_xh)
            node_yh = normalize_y(node_yh)
            ctx.set_line_width(line_width)


            # draw fixed macros
            ctx.set_source_rgba(1, 0, 0, alpha=0.5)
            for i in range(num_movable_nodes, num_physical_nodes):
                ctx.rectangle(node_xl[i], node_yl[i], node_xh[i] - node_xl[i],
                              node_yh[i] -
                              node_yl[i])  # Rectangle(xl, yl, w, h)
                ctx.fill()
            ctx.set_source_rgba(0, 0, 0, alpha=1.0)  # Solid color
            for i in range(num_movable_nodes, num_physical_nodes):
                draw_rect(node_xl[i], node_yl[i], node_xh[i], node_yh[i])
            # draw fillers
            if len(node_xl) > num_physical_nodes:  # filler is included
                ctx.set_line_width(line_width)
                ctx.set_source_rgba(115 / 255.0,
                                    115 / 255.0,
                                    125 / 255.0,
                                    alpha=0.5)  # Solid color
                for i in range(num_physical_nodes, num_nodes):
                    ctx.rectangle(node_xl[i], node_yl[i],
                                  node_xh[i] - node_xl[i], node_yh[i] -
                                  node_yl[i])  # Rectangle(xl, yl, w, h)
                    ctx.fill()
                ctx.set_source_rgba(230 / 255.0,
                                    230 / 255.0,
                                    250 / 255.0,
                                    alpha=0.3)  # Solid color
                for i in range(num_physical_nodes, num_nodes):
                    draw_rect(node_xl[i], node_yl[i], node_xh[i], node_yh[i])
            # draw cells
            ctx.set_line_width(line_width * 2)
            ctx.set_source_rgba(0, 0, 1, alpha=0.5)  # Solid color
            for i in range(num_movable_nodes):
                ctx.rectangle(node_xl[i], node_yl[i], node_xh[i] - node_xl[i],
                              node_yh[i] -
                              node_yl[i])  # Rectangle(xl, yl, w, h)
                ctx.fill()
            ctx.set_source_rgba(0, 0, 0.8, alpha=0.8)  # Solid color
            for i in range(num_movable_nodes):
                draw_rect(node_xl[i], node_yl[i], node_xh[i], node_yh[i])
            ## draw cell indices
            # but only for small grids
            if xh < 10 and yh < 10:
                for i in range(num_nodes):
                    ctx.move_to((node_xl[i]+node_xh[i])/2, (node_yl[i]+node_yh[i])/2)
                    ctx.show_text("%d" % (i))

            # show iteration
            if iteration:
                ctx.set_source_rgb(0, 0, 0)
                ctx.set_line_width(line_width * 10)
                ctx.select_font_face("monospace", cairo.FONT_SLANT_NORMAL,
                                     cairo.FONT_WEIGHT_NORMAL)
                ctx.set_font_size(32)
                #ctx.move_to(normalize_x((xl + xh) / 2),
                #            normalize_y((yl + yh) / 2))
                ctx.move_to(normalize_x(xl)+1, normalize_y(yl)+1)
                ctx.show_text('{:4}'.format(iteration))

            surface.write_to_png(filename)  # Output to PNG
            print("[I] plotting to %s takes %.3f seconds" %
                  (filename, time.time() - tt))
        except Exception as e:
            print("[E] failed to plot")
            print(str(e))
            return 0

        return 1