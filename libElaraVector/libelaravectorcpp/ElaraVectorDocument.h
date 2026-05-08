#ifndef ELARA_VECTOR_DOCUMENT_H
#define ELARA_VECTOR_DOCUMENT_H

#include <libelaravector/elara_vector.h>

namespace elara {

enum ElaraOverlayAnchorH {
    ELARA_OVERLAY_ANCHOR_LEFT  = 0,
    ELARA_OVERLAY_ANCHOR_RIGHT = 1
};

enum ElaraOverlayAnchorV {
    ELARA_OVERLAY_ANCHOR_TOP    = 0,
    ELARA_OVERLAY_ANCHOR_BOTTOM = 1
};

class ElaraVectorDocument {
private:
    EvDocument *document;
    float ox;
    float oy;
    ElaraOverlayAnchorH anchor_h;
    ElaraOverlayAnchorV anchor_v;

public:
    ElaraVectorDocument();
    ElaraVectorDocument(const ElaraVectorDocument& other);
    ElaraVectorDocument& operator=(const ElaraVectorDocument& other);
    ~ElaraVectorDocument();

    void setDocument(EvDocument *doc);
    EvDocument *getDocument() const;

    void setPosition(float x, float y);
    float getX() const;
    float getY() const;

    void setAnchorH(ElaraOverlayAnchorH anchor);
    void setAnchorV(ElaraOverlayAnchorV anchor);
    ElaraOverlayAnchorH getAnchorH() const;
    ElaraOverlayAnchorV getAnchorV() const;
};

}

#endif
