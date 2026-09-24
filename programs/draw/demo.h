#ifndef DRAW_DEMO_H
#define DRAW_DEMO_H

struct graphics;

// Draws a black outline landscape (mountains, river, house, trees, birds, sunset, people)
// scaled to fit the given canvas rectangle
void demo_draw_scene(struct graphics *canvas, int x, int y, int width, int height);

#endif
