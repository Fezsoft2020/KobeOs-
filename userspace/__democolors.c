/* Copyright © 2018-2020 N. Van Bossuyt.                                      */
/* This code is licensed under the MIT License.                               */
/* See: LICENSE.md                                                            */

#include "DemoCommon.h"

void draw(Painter *painter, Rectangle screen, double time)
{
    for (int x = 0; x < screen.width; x++)
    {
        for (int y = 0; y < screen.height; y++)
        {
            Color color = HSV((x / (float)screen.width) * 360.0 + time, (y / (float)screen.height), 1.0);
            painter_plot_pixel(painter, (Point){screen.X + x, screen.Y + y}, color);
        }
    }
}

int main(int argc, char **argv)
{
    return demo_start(argc, argv, "Colors", draw);
}