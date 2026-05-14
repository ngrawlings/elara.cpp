#ifndef ELARA_PALETTE_H
#define ELARA_PALETTE_H

#include <libelaracore/memory/String.h>
#include <libelaracore/memory/Array.h>

namespace elara {

class ElaraColor {
public:
    double r;
    double g;
    double b;
    double a;

    ElaraColor();
    ElaraColor(double red, double green, double blue, double alpha = 1.0);
};

class ElaraPaletteTriplet {
public:
    ElaraColor base;
    ElaraColor accent;
    ElaraColor text;
    double border_width;
    double corner_radius;

    ElaraPaletteTriplet();
    ElaraPaletteTriplet(
        const ElaraColor& base_color,
        const ElaraColor& accent_color,
        const ElaraColor& text_color
    );
};

class ElaraPaletteEntry {
public:
    String master;
    String sub;
    ElaraPaletteTriplet colors;

    ElaraPaletteEntry();
    ElaraPaletteEntry(
        const String& master_profile,
        const String& sub_profile,
        const ElaraPaletteTriplet& triplet
    );
};

class ElaraPalette {
private:
    Array<ElaraPaletteEntry> entries;
    ElaraPaletteTriplet defaults;

    int findEntry(const String& master, const String& sub) const;

public:
    ElaraPalette();

    void setDefaults(const ElaraPaletteTriplet& triplet);
    ElaraPaletteTriplet getDefaults() const;

    void set(
        const String& master,
        const String& sub,
        const ElaraPaletteTriplet& triplet
    );

    ElaraPaletteTriplet get(const String& master, const String& sub) const;

    ElaraColor base(const String& master, const String& sub) const;
    ElaraColor accent(const String& master, const String& sub) const;
    ElaraColor text(const String& master, const String& sub) const;

    void clear();
};

}

#endif
