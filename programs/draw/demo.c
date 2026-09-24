#include "demo.h"
#include "graphics.h"

// The scene is laid out on a 1000 x 650 grid and scaled to fit the canvas, so circles stay round
#define GRID_WIDTH 1000
#define GRID_HEIGHT 650
#define DEMO_BRUSH 2
#define CIRCLE_STEPS 24
#define GROUND_Y 400

// Defined in draw.c
void draw_line(struct graphics *canvas, int x0, int y0, int x1, int y1, int brush_size, struct framebuffer_pixel color);

// cos and sin of k * 15 degrees, times 1000. Screen y grows downward, so a negative
// sin is the upper half of a circle.
static const int cos_table[CIRCLE_STEPS] = {
    1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966,
    -1000, -966, -866, -707, -500, -259, 0, 259, 500, 707, 866, 966};

static int cos_k(int k)
{
    return cos_table[k % CIRCLE_STEPS];
}

static int sin_k(int k)
{
    return cos_table[(k + 18) % CIRCLE_STEPS];
}

static struct
{
    struct graphics *canvas;
    int origin_x;
    int origin_y;
    int scale;
} scene;

static struct framebuffer_pixel black = {.red = 0, .green = 0, .blue = 0, .reserved = 0};

static int map_x(int u)
{
    return scene.origin_x + u * scene.scale / GRID_WIDTH;
}

static int map_y(int v)
{
    return scene.origin_y + v * scene.scale / GRID_WIDTH;
}

static void segment(int x0, int y0, int x1, int y1)
{
    draw_line(scene.canvas, map_x(x0), map_y(y0), map_x(x1), map_y(y1), DEMO_BRUSH, black);
}

// Connects a list of (x, y) pairs
static void polyline(const int *points, int count)
{
    for (int i = 0; i + 1 < count; i++)
    {
        segment(points[i * 2], points[i * 2 + 1], points[i * 2 + 2], points[i * 2 + 3]);
    }
}

// From step first to step last of a circle
static void arc(int cx, int cy, int radius, int first, int last)
{
    for (int k = first; k < last; k++)
    {
        segment(cx + radius * cos_k(k) / 1000, cy + radius * sin_k(k) / 1000,
                cx + radius * cos_k(k + 1) / 1000, cy + radius * sin_k(k + 1) / 1000);
    }
}

static void box(int x, int y, int width, int height)
{
    segment(x, y, x + width, y);
    segment(x + width, y, x + width, y + height);
    segment(x + width, y + height, x, y + height);
    segment(x, y + height, x, y);
}

// The far ridge, in front of the sun
#define FAR_RIDGE_POINTS 5
static const int far_ridge[] = {700, 400, 770, 340, 830, 375, 900, 320, 980, 400};

// Height of the far ridge at x, or the ground line outside it
static int far_ridge_y(int x)
{
    for (int i = 0; i + 1 < FAR_RIDGE_POINTS; i++)
    {
        int x0 = far_ridge[i * 2];
        int y0 = far_ridge[i * 2 + 1];
        int x1 = far_ridge[i * 2 + 2];
        int y1 = far_ridge[i * 2 + 3];
        if (x >= x0 && x <= x1)
        {
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        }
    }
    return GROUND_Y;
}

// A segment of the sun: only the pieces above the far ridge are drawn, the rest is hidden behind it
#define SUN_CLIP_STEPS 16
static void sun_segment(int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < SUN_CLIP_STEPS; i++)
    {
        int ax = x0 + (x1 - x0) * i / SUN_CLIP_STEPS;
        int ay = y0 + (y1 - y0) * i / SUN_CLIP_STEPS;
        int bx = x0 + (x1 - x0) * (i + 1) / SUN_CLIP_STEPS;
        int by = y0 + (y1 - y0) * (i + 1) / SUN_CLIP_STEPS;
        if ((ay + by) / 2 < far_ridge_y((ax + bx) / 2))
        {
            segment(ax, ay, bx, by);
        }
    }
}

static void sun(int cx, int cy, int radius)
{
    for (int k = 12; k < 24; k++)
    {
        sun_segment(cx + radius * cos_k(k) / 1000, cy + radius * sin_k(k) / 1000,
                    cx + radius * cos_k(k + 1) / 1000, cy + radius * sin_k(k + 1) / 1000);
    }
    for (int k = 12; k <= 24; k += 2)
    {
        sun_segment(cx + (radius + 15) * cos_k(k) / 1000, cy + (radius + 15) * sin_k(k) / 1000,
                    cx + (radius + 42) * cos_k(k) / 1000, cy + (radius + 42) * sin_k(k) / 1000);
    }
}

static void mountains()
{
    static const int range[] = {20, 400, 150, 250, 210, 300, 330, 120, 400, 210, 450, 180, 560, 330, 640, 400};
    polyline(range, 8);

    // Snow cap on the tallest peak
    static const int snow[] = {293, 175, 312, 190, 330, 172, 350, 190, 373, 175};
    polyline(snow, 5);

    polyline(far_ridge, FAR_RIDGE_POINTS);
}

static void river()
{
    static const int left_bank[] = {500, 400, 480, 450, 520, 500, 470, 560, 500, 640};
    static const int right_bank[] = {540, 400, 565, 450, 625, 500, 600, 560, 690, 640};
    polyline(left_bank, 5);
    polyline(right_bank, 5);
}

static void house(int x, int base, int width, int height)
{
    int roof_peak = base - height - height * 3 / 5;
    box(x, base - height, width, height);

    // Roof, overhanging a little
    segment(x - 12, base - height, x + width / 2, roof_peak);
    segment(x + width / 2, roof_peak, x + width + 12, base - height);

    // Chimney on the roof's right slope
    int chimney_x = x + width * 3 / 4;
    int slope_y = roof_peak + (base - height - roof_peak) * (chimney_x - (x + width / 2)) / (width / 2 + 12);
    int slope_y2 = roof_peak + (base - height - roof_peak) * (chimney_x + 18 - (x + width / 2)) / (width / 2 + 12);
    segment(chimney_x, slope_y, chimney_x, slope_y - 25);
    segment(chimney_x, slope_y - 25, chimney_x + 18, slope_y - 25);
    segment(chimney_x + 18, slope_y - 25, chimney_x + 18, slope_y2);

    box(x + width / 2 - 15, base - height * 3 / 5, 30, height * 3 / 5);
    box(x + 15, base - height * 3 / 4, 30, 30);
}

// A pine: a trunk under two stacked triangles
static void tree(int cx, int base, int height)
{
    int trunk_top = base - height / 5;
    segment(cx, trunk_top, cx, base);
    int low[] = {cx - height / 4, trunk_top, cx, base - height * 3 / 5, cx + height / 4, trunk_top, cx - height / 4, trunk_top};
    polyline(low, 4);
    int high[] = {cx - height / 5, base - height / 2, cx, base - height, cx + height / 5, base - height / 2, cx - height / 5, base - height / 2};
    polyline(high, 4);
}

static void bird(int x, int y)
{
    int wings[] = {x - 18, y - 6, x - 9, y - 11, x, y, x + 9, y - 11, x + 18, y - 6};
    polyline(wings, 5);
}

static void person(int x, int feet, int height)
{
    int head = height / 8;
    int neck = feet - height + head * 2;
    int hip = feet - height * 2 / 5;
    int shoulder = neck + height / 10;

    arc(x, feet - height + head, head, 0, CIRCLE_STEPS);
    segment(x, neck, x, hip);
    segment(x, shoulder, x - height / 5, hip - height / 10);
    segment(x, shoulder, x + height / 5, hip - height / 10);
    segment(x, hip, x - height / 8, feet);
    segment(x, hip, x + height / 8, feet);
}

void demo_draw_scene(struct graphics *canvas, int x, int y, int width, int height)
{
    int scale = width;
    if (height * GRID_WIDTH / GRID_HEIGHT < scale)
    {
        scale = height * GRID_WIDTH / GRID_HEIGHT;
    }

    scene.canvas = canvas;
    scene.scale = scale;
    scene.origin_x = x + (width - scale) / 2;
    scene.origin_y = y + (height - scale * GRID_HEIGHT / GRID_WIDTH) / 2;

    // Horizon
    segment(20, GROUND_Y, 980, GROUND_Y);

    sun(830, GROUND_Y, 70);
    mountains();
    river();

    bird(470, 70);
    bird(560, 110);
    bird(640, 60);
    bird(720, 130);
    bird(790, 80);

    house(110, 570, 150, 90);
    tree(50, 560, 100);
    tree(370, 540, 100);
    tree(770, 610, 150);
    tree(880, 570, 120);
    tree(950, 620, 130);

    person(260, 620, 100);
    person(300, 625, 70);
}
