#ifndef ELARA_VECTOR_H
#define ELARA_VECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef enum {
    EV_NODE_GROUP,
    EV_NODE_RECT,
    EV_NODE_CIRCLE,
    EV_NODE_LINE,
    EV_NODE_POLYLINE,
    EV_NODE_POLYGON,
    EV_NODE_PATH,
    EV_NODE_TEXT,
    EV_NODE_IMAGE,
    EV_NODE_USE
} EvNodeType;

typedef enum {
    EV_SEG_MOVE,
    EV_SEG_LINE,
    EV_SEG_QUAD,
    EV_SEG_CUBIC,
    EV_SEG_CLOSE
} EvPathSegType;

typedef struct {
    float x;
    float y;
} EvPoint;

typedef struct {
    unsigned char r, g, b, a;
} EvColor;

typedef struct {
    EvColor fill;
    EvColor stroke;
    float stroke_width;

    unsigned char has_fill;
    unsigned char has_stroke;
} EvStyle;

typedef struct {
    float x;
    float y;
    float scale_x;
    float scale_y;
    float rotation;
} EvTransform;

typedef struct {
    EvPathSegType type;

    union {
        struct { float x, y; } move;
        struct { float x, y; } line;
        struct { float cx, cy, x, y; } quad;
        struct { float c1x, c1y, c2x, c2y, x, y; } cubic;
    } data;
} EvPathSeg;

typedef struct EvNode EvNode;

struct EvNode {
    EvNodeType type;

    char *id;
    char *ref;

    EvTransform transform;
    EvStyle style;

    union {
        struct {
            float x, y, w, h, radius;
        } rect;

        struct {
            float x, y, r;
        } circle;

        struct {
            float x1, y1, x2, y2;
        } line;

        struct {
            EvPoint *points;
            size_t count;
        } points;

        struct {
            EvPathSeg *segments;
            size_t count;
        } path;

        struct {
            float x, y;
            char *text;
            char *font;
            float size;
        } text;

        struct {
            float x, y, w, h;
            char *src;
        } image;
    } data;

    EvNode **children;
    size_t child_count;
    size_t child_capacity;
};

typedef struct {
    int version;
    float width;
    float height;

    EvNode **children;
    size_t child_count;
    size_t child_capacity;
} EvDocument;

/*
    Allocation / destruction
*/

EvDocument *ev_document_create(float width, float height);
void ev_document_free(EvDocument *doc);

EvNode *ev_node_create(EvNodeType type);
void ev_node_free(EvNode *node);

int ev_document_add_child(EvDocument *doc, EvNode *child);
int ev_node_add_child(EvNode *parent, EvNode *child);

/*
    Convenience constructors
*/

EvNode *ev_rect(float x, float y, float w, float h);
EvNode *ev_circle(float x, float y, float r);
EvNode *ev_line(float x1, float y1, float x2, float y2);
EvNode *ev_group(void);
EvNode *ev_path(void);

/*
    Path building
*/

int ev_path_move_to(EvNode *path, float x, float y);
int ev_path_line_to(EvNode *path, float x, float y);
int ev_path_quad_to(EvNode *path, float cx, float cy, float x, float y);
int ev_path_cubic_to(EvNode *path, float c1x, float c1y, float c2x, float c2y, float x, float y);
int ev_path_close(EvNode *path);

/*
    Styling
*/

EvColor ev_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void ev_set_fill(EvNode *node, EvColor color);
void ev_set_stroke(EvNode *node, EvColor color, float width);

/*
    Renderer traversal.
    Children are always rendered after the parent.
    Later siblings are rendered above earlier siblings.
*/

typedef struct {
    void (*begin_document)(EvDocument *doc, void *user);
    void (*end_document)(EvDocument *doc, void *user);

    void (*draw_node)(EvNode *node, void *user);
} EvRenderer;

void ev_render_document(EvDocument *doc, EvRenderer *renderer, void *user);

#ifdef __cplusplus
}
#endif

#endif
