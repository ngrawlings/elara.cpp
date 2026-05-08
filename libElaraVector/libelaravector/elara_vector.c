#include "elara_vector.h"

#include <stdlib.h>
#include <string.h>

static void *ev_realloc_array(void *ptr, size_t count, size_t size) {
    if (count == 0 || size == 0) return NULL;
    if (count > ((size_t)-1) / size) return NULL;
    return realloc(ptr, count * size);
}

static EvTransform ev_default_transform(void) {
    EvTransform tr;
    tr.x = 0.0f;
    tr.y = 0.0f;
    tr.scale_x = 1.0f;
    tr.scale_y = 1.0f;
    tr.rotation = 0.0f;
    return tr;
}

static EvStyle ev_default_style(void) {
    EvStyle s;
    memset(&s, 0, sizeof(EvStyle));
    s.stroke_width = 1.0f;
    return s;
}

EvColor ev_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    EvColor c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

EvDocument *ev_document_create(float width, float height) {
    EvDocument *doc = (EvDocument *)calloc(1, sizeof(EvDocument));
    if (!doc) return NULL;

    doc->version = 1;
    doc->width = width;
    doc->height = height;

    return doc;
}

void ev_document_free(EvDocument *doc) {
    size_t i;

    if (!doc) return;

    for (i = 0; i < doc->child_count; ++i) {
        ev_node_free(doc->children[i]);
    }

    free(doc->children);
    free(doc);
}

EvNode *ev_node_create(EvNodeType type) {
    EvNode *node = (EvNode *)calloc(1, sizeof(EvNode));
    if (!node) return NULL;

    node->type = type;
    node->transform = ev_default_transform();
    node->style = ev_default_style();

    return node;
}

void ev_node_free(EvNode *node) {
    size_t i;

    if (!node) return;

    free(node->id);
    free(node->ref);

    switch (node->type) {
        case EV_NODE_POLYLINE:
        case EV_NODE_POLYGON:
            free(node->data.points.points);
            break;

        case EV_NODE_PATH:
            free(node->data.path.segments);
            break;

        case EV_NODE_TEXT:
            free(node->data.text.text);
            free(node->data.text.font);
            break;

        case EV_NODE_IMAGE:
            free(node->data.image.src);
            break;

        default:
            break;
    }

    for (i = 0; i < node->child_count; ++i) {
        ev_node_free(node->children[i]);
    }

    free(node->children);
    free(node);
}

int ev_document_add_child(EvDocument *doc, EvNode *child) {
    EvNode **grown;
    size_t next_capacity;

    if (!doc || !child) return 0;

    if (doc->child_count == doc->child_capacity) {
        next_capacity = doc->child_capacity ? doc->child_capacity * 2 : 4;
        grown = (EvNode **)ev_realloc_array(doc->children, next_capacity, sizeof(EvNode *));
        if (!grown) return 0;

        doc->children = grown;
        doc->child_capacity = next_capacity;
    }

    doc->children[doc->child_count++] = child;
    return 1;
}

int ev_node_add_child(EvNode *parent, EvNode *child) {
    EvNode **grown;
    size_t next_capacity;

    if (!parent || !child) return 0;

    if (parent->child_count == parent->child_capacity) {
        next_capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
        grown = (EvNode **)ev_realloc_array(parent->children, next_capacity, sizeof(EvNode *));
        if (!grown) return 0;

        parent->children = grown;
        parent->child_capacity = next_capacity;
    }

    parent->children[parent->child_count++] = child;
    return 1;
}

EvNode *ev_group(void) {
    return ev_node_create(EV_NODE_GROUP);
}

EvNode *ev_rect(float x, float y, float w, float h) {
    EvNode *node = ev_node_create(EV_NODE_RECT);
    if (!node) return NULL;

    node->data.rect.x = x;
    node->data.rect.y = y;
    node->data.rect.w = w;
    node->data.rect.h = h;
    node->data.rect.radius = 0.0f;

    return node;
}

EvNode *ev_circle(float x, float y, float r) {
    EvNode *node = ev_node_create(EV_NODE_CIRCLE);
    if (!node) return NULL;

    node->data.circle.x = x;
    node->data.circle.y = y;
    node->data.circle.r = r;

    return node;
}

EvNode *ev_line(float x1, float y1, float x2, float y2) {
    EvNode *node = ev_node_create(EV_NODE_LINE);
    if (!node) return NULL;

    node->data.line.x1 = x1;
    node->data.line.y1 = y1;
    node->data.line.x2 = x2;
    node->data.line.y2 = y2;

    return node;
}

EvNode *ev_path(void) {
    return ev_node_create(EV_NODE_PATH);
}

static int ev_path_add_segment(EvNode *path, EvPathSeg seg) {
    EvPathSeg *grown;
    size_t next_count;

    if (!path || path->type != EV_NODE_PATH) return 0;

    next_count = path->data.path.count + 1;

    grown = (EvPathSeg *)ev_realloc_array(
        path->data.path.segments,
        next_count,
        sizeof(EvPathSeg)
    );

    if (!grown) return 0;

    path->data.path.segments = grown;
    path->data.path.segments[path->data.path.count] = seg;
    path->data.path.count = next_count;

    return 1;
}

int ev_path_move_to(EvNode *path, float x, float y) {
    EvPathSeg seg;
    memset(&seg, 0, sizeof(seg));

    seg.type = EV_SEG_MOVE;
    seg.data.move.x = x;
    seg.data.move.y = y;

    return ev_path_add_segment(path, seg);
}

int ev_path_line_to(EvNode *path, float x, float y) {
    EvPathSeg seg;
    memset(&seg, 0, sizeof(seg));

    seg.type = EV_SEG_LINE;
    seg.data.line.x = x;
    seg.data.line.y = y;

    return ev_path_add_segment(path, seg);
}

int ev_path_quad_to(EvNode *path, float cx, float cy, float x, float y) {
    EvPathSeg seg;
    memset(&seg, 0, sizeof(seg));

    seg.type = EV_SEG_QUAD;
    seg.data.quad.cx = cx;
    seg.data.quad.cy = cy;
    seg.data.quad.x = x;
    seg.data.quad.y = y;

    return ev_path_add_segment(path, seg);
}

int ev_path_cubic_to(EvNode *path, float c1x, float c1y, float c2x, float c2y, float x, float y) {
    EvPathSeg seg;
    memset(&seg, 0, sizeof(seg));

    seg.type = EV_SEG_CUBIC;
    seg.data.cubic.c1x = c1x;
    seg.data.cubic.c1y = c1y;
    seg.data.cubic.c2x = c2x;
    seg.data.cubic.c2y = c2y;
    seg.data.cubic.x = x;
    seg.data.cubic.y = y;

    return ev_path_add_segment(path, seg);
}

int ev_path_close(EvNode *path) {
    EvPathSeg seg;
    memset(&seg, 0, sizeof(seg));

    seg.type = EV_SEG_CLOSE;

    return ev_path_add_segment(path, seg);
}

void ev_set_fill(EvNode *node, EvColor color) {
    if (!node) return;

    node->style.fill = color;
    node->style.has_fill = 1;
}

void ev_set_stroke(EvNode *node, EvColor color, float width) {
    if (!node) return;

    node->style.stroke = color;
    node->style.stroke_width = width;
    node->style.has_stroke = 1;
}

static void ev_render_node(EvNode *node, EvRenderer *renderer, void *user) {
    size_t i;

    if (!node || !renderer) return;

    if (renderer->draw_node) {
        renderer->draw_node(node, user);
    }

    /*
        Z-order rule:
        children render after parent,
        later children render above earlier children.
    */
    for (i = 0; i < node->child_count; ++i) {
        ev_render_node(node->children[i], renderer, user);
    }
}

void ev_render_document(EvDocument *doc, EvRenderer *renderer, void *user) {
    size_t i;

    if (!doc || !renderer) return;

    if (renderer->begin_document) {
        renderer->begin_document(doc, user);
    }

    for (i = 0; i < doc->child_count; ++i) {
        ev_render_node(doc->children[i], renderer, user);
    }

    if (renderer->end_document) {
        renderer->end_document(doc, user);
    }
}
