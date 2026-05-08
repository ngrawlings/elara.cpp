#include "ElaraVectorDocument.h"

namespace elara {

ElaraVectorDocument::ElaraVectorDocument()
    : document(0), ox(0.0f), oy(0.0f),
      anchor_h(ELARA_OVERLAY_ANCHOR_LEFT),
      anchor_v(ELARA_OVERLAY_ANCHOR_TOP) {}

ElaraVectorDocument::ElaraVectorDocument(const ElaraVectorDocument& other)
    : document(other.document), ox(other.ox), oy(other.oy),
      anchor_h(other.anchor_h), anchor_v(other.anchor_v) {
    const_cast<ElaraVectorDocument&>(other).document = 0;
}

ElaraVectorDocument& ElaraVectorDocument::operator=(const ElaraVectorDocument& other) {
    if (this != &other) {
        if (document) {
            ev_document_free(document);
        }
        document = other.document;
        ox = other.ox;
        oy = other.oy;
        anchor_h = other.anchor_h;
        anchor_v = other.anchor_v;
        const_cast<ElaraVectorDocument&>(other).document = 0;
    }
    return *this;
}

ElaraVectorDocument::~ElaraVectorDocument() {
    if (document) {
        ev_document_free(document);
        document = 0;
    }
}

void ElaraVectorDocument::setDocument(EvDocument *doc) {
    if (document) {
        ev_document_free(document);
    }
    document = doc;
}

EvDocument *ElaraVectorDocument::getDocument() const {
    return document;
}

void ElaraVectorDocument::setPosition(float x, float y) {
    ox = x;
    oy = y;
}

float ElaraVectorDocument::getX() const { return ox; }
float ElaraVectorDocument::getY() const { return oy; }

void ElaraVectorDocument::setAnchorH(ElaraOverlayAnchorH anchor) { anchor_h = anchor; }
void ElaraVectorDocument::setAnchorV(ElaraOverlayAnchorV anchor) { anchor_v = anchor; }
ElaraOverlayAnchorH ElaraVectorDocument::getAnchorH() const { return anchor_h; }
ElaraOverlayAnchorV ElaraVectorDocument::getAnchorV() const { return anchor_v; }

}
