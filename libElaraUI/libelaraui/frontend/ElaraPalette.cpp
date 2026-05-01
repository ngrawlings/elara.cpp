#include "ElaraPalette.h"

namespace elara {

ElaraColor::ElaraColor()
    : r(0),
      g(0),
      b(0),
      a(1.0) {}

ElaraColor::ElaraColor(double red, double green, double blue, double alpha)
    : r(red),
      g(green),
      b(blue),
      a(alpha) {}

ElaraPaletteTriplet::ElaraPaletteTriplet()
    : base(ElaraColor(0.08, 0.08, 0.10)),
      accent(ElaraColor(0.22, 0.22, 0.28)),
      text(ElaraColor(0.88, 0.88, 0.92)) {}

ElaraPaletteTriplet::ElaraPaletteTriplet(
    const ElaraColor& base_color,
    const ElaraColor& accent_color,
    const ElaraColor& text_color
) : base(base_color),
    accent(accent_color),
    text(text_color) {}

ElaraPaletteEntry::ElaraPaletteEntry() {}

ElaraPaletteEntry::ElaraPaletteEntry(
    const String& master_profile,
    const String& sub_profile,
    const ElaraPaletteTriplet& triplet
) : master(master_profile),
    sub(sub_profile),
    colors(triplet) {}

ElaraPalette::ElaraPalette()
    : defaults() {}

int ElaraPalette::findEntry(const String& master, const String& sub) const {
    for(int i = 0; i < (int)entries.length(); i++) {
        if(entries[i].master == master && entries[i].sub == sub) {
            return i;
        }
    }

    return -1;
}

void ElaraPalette::setDefaults(const ElaraPaletteTriplet& triplet) {
    defaults = triplet;
}

ElaraPaletteTriplet ElaraPalette::getDefaults() const {
    return defaults;
}

void ElaraPalette::set(
    const String& master,
    const String& sub,
    const ElaraPaletteTriplet& triplet
) {
    int index = findEntry(master, sub);

    if(index >= 0) {
        entries[index].colors = triplet;
        return;
    }

    entries.push(ElaraPaletteEntry(master, sub, triplet));
}

ElaraPaletteTriplet ElaraPalette::get(const String& master, const String& sub) const {
    int index = findEntry(master, sub);

    if(index >= 0) {
        return entries[index].colors;
    }

    index = findEntry(master, String("default"));

    if(index >= 0) {
        return entries[index].colors;
    }

    return defaults;
}

ElaraColor ElaraPalette::base(const String& master, const String& sub) const {
    return get(master, sub).base;
}

ElaraColor ElaraPalette::accent(const String& master, const String& sub) const {
    return get(master, sub).accent;
}

ElaraColor ElaraPalette::text(const String& master, const String& sub) const {
    return get(master, sub).text;
}

void ElaraPalette::clear() {
    entries.clear();
}

}
