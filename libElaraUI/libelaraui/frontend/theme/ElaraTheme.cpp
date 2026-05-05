#include "ElaraTheme.h"

namespace elara {

ElaraThemeProfile::ElaraThemeProfile()
    : name("default") {}

ElaraThemeProfile::ElaraThemeProfile(const String& profile_name)
    : name(profile_name) {}

void ElaraThemeProfile::setName(const String& profile_name) {
    name = profile_name;
}

String ElaraThemeProfile::getName() const {
    return name;
}

ElaraPalette* ElaraThemeProfile::getPalette() {
    return &palette;
}

const ElaraPalette* ElaraThemeProfile::getPalette() const {
    return &palette;
}

ElaraTheme::ElaraTheme()
    : dark_profile("dark"),
      light_profile("light"),
      active_profile(&dark_profile) {
    buildDark();
    buildLight();
}

bool ElaraTheme::setMode(const String& mode) {
    if(mode == String("dark")) {
        active_profile = &dark_profile;
        return true;
    }

    if(mode == String("light")) {
        active_profile = &light_profile;
        return true;
    }

    return false;
}

String ElaraTheme::getMode() const {
    if(!active_profile) {
        return String("");
    }

    return active_profile->getName();
}

ElaraPalette* ElaraTheme::getPalette() {
    if(!active_profile) {
        return 0;
    }

    return active_profile->getPalette();
}

const ElaraPalette* ElaraTheme::getPalette() const {
    if(!active_profile) {
        return 0;
    }

    return active_profile->getPalette();
}

ElaraThemeProfile* ElaraTheme::getDarkProfile() {
    return &dark_profile;
}

ElaraThemeProfile* ElaraTheme::getLightProfile() {
    return &light_profile;
}

void ElaraTheme::buildDark() {
    ElaraPalette* palette = dark_profile.getPalette();

    palette->clear();

    palette->setDefaults(
        ElaraPaletteTriplet(
            ElaraColor(0.08, 0.08, 0.10),
            ElaraColor(0.22, 0.22, 0.28),
            ElaraColor(0.88, 0.88, 0.92)
        )
    );

    palette->set("tabs", "active",
        ElaraPaletteTriplet(
            ElaraColor(0.28, 0.32, 0.46),  // base: active fill
            ElaraColor(0.55, 0.65, 1.00),  // accent: active underline
            ElaraColor(1.00, 1.00, 1.00)   // text
        )
    );

    palette->set("tabs", "hover",
        ElaraPaletteTriplet(
            ElaraColor(0.17, 0.17, 0.21),
            ElaraColor(0.25, 0.25, 0.30),
            ElaraColor(0.88, 0.88, 0.92)
        )
    );

    palette->set("tabs", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.11, 0.11, 0.14),
            ElaraColor(0.20, 0.20, 0.24),
            ElaraColor(0.80, 0.80, 0.90)
        )
    );

    palette->set("panel", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.10, 0.10, 0.12),
            ElaraColor(0.20, 0.20, 0.25),
            ElaraColor(0.85, 0.85, 0.90)
        )
    );

    palette->set("button", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.18, 0.20, 0.26),
            ElaraColor(0.42, 0.48, 0.62),
            ElaraColor(0.96, 0.97, 1.00)
        )
    );

    palette->set("button", "hover",
        ElaraPaletteTriplet(
            ElaraColor(0.24, 0.27, 0.36),
            ElaraColor(0.56, 0.64, 0.86),
            ElaraColor(1.00, 1.00, 1.00)
        )
    );

    palette->set("button", "pressed",
        ElaraPaletteTriplet(
            ElaraColor(0.10, 0.38, 0.78),
            ElaraColor(0.72, 0.84, 1.00),
            ElaraColor(1.00, 1.00, 1.00)
        )
    );

    palette->set("button", "disabled",
        ElaraPaletteTriplet(
            ElaraColor(0.16, 0.16, 0.18),
            ElaraColor(0.26, 0.26, 0.30),
            ElaraColor(0.56, 0.56, 0.60)
        )
    );

    palette->set("graph", "line",
        ElaraPaletteTriplet(
            ElaraColor(0.10, 0.10, 0.12),
            ElaraColor(0.90, 0.50, 0.20),
            ElaraColor(0.90, 0.90, 0.95)
        )
    );

    palette->set("popup", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.12, 0.12, 0.15),
            ElaraColor(0.35, 0.35, 0.42),
            ElaraColor(0.90, 0.90, 0.94)
        )
    );

    palette->set("popup", "hover",
        ElaraPaletteTriplet(
            ElaraColor(0.25, 0.30, 0.48),
            ElaraColor(0.45, 0.50, 0.75),
            ElaraColor(1.00, 1.00, 1.00)
        )
    );
}

void ElaraTheme::buildLight() {
    ElaraPalette* palette = light_profile.getPalette();

    palette->clear();

    palette->setDefaults(
        ElaraPaletteTriplet(
            ElaraColor(0.95, 0.95, 0.97),
            ElaraColor(0.80, 0.80, 0.85),
            ElaraColor(0.10, 0.10, 0.12)
        )
    );

    palette->set("tabs", "active",
        ElaraPaletteTriplet(
            ElaraColor(0.80, 0.80, 0.85),
            ElaraColor(0.65, 0.65, 0.75),
            ElaraColor(0.05, 0.05, 0.08)
        )
    );

    palette->set("tabs", "hover",
        ElaraPaletteTriplet(
            ElaraColor(0.88, 0.88, 0.92),
            ElaraColor(0.70, 0.70, 0.78),
            ElaraColor(0.10, 0.10, 0.12)
        )
    );

    palette->set("tabs", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.92, 0.92, 0.95),
            ElaraColor(0.75, 0.75, 0.80),
            ElaraColor(0.15, 0.15, 0.18)
        )
    );

    palette->set("panel", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.97, 0.97, 0.99),
            ElaraColor(0.80, 0.80, 0.85),
            ElaraColor(0.12, 0.12, 0.15)
        )
    );

    palette->set("button", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.86, 0.89, 0.96),
            ElaraColor(0.56, 0.63, 0.82),
            ElaraColor(0.10, 0.12, 0.18)
        )
    );

    palette->set("button", "hover",
        ElaraPaletteTriplet(
            ElaraColor(0.78, 0.84, 0.98),
            ElaraColor(0.40, 0.52, 0.84),
            ElaraColor(0.08, 0.10, 0.16)
        )
    );

    palette->set("button", "pressed",
        ElaraPaletteTriplet(
            ElaraColor(0.28, 0.50, 0.94),
            ElaraColor(0.14, 0.32, 0.74),
            ElaraColor(1.00, 1.00, 1.00)
        )
    );

    palette->set("button", "disabled",
        ElaraPaletteTriplet(
            ElaraColor(0.92, 0.92, 0.94),
            ElaraColor(0.80, 0.80, 0.84),
            ElaraColor(0.56, 0.56, 0.60)
        )
    );

    palette->set("graph", "line",
        ElaraPaletteTriplet(
            ElaraColor(0.97, 0.97, 0.99),
            ElaraColor(0.20, 0.50, 0.90),
            ElaraColor(0.10, 0.10, 0.12)
        )
    );

    palette->set("popup", "default",
        ElaraPaletteTriplet(
            ElaraColor(0.98, 0.98, 1.00),
            ElaraColor(0.65, 0.65, 0.72),
            ElaraColor(0.10, 0.10, 0.12)
        )
    );

    palette->set("popup", "hover",
        ElaraPaletteTriplet(
            ElaraColor(0.78, 0.84, 1.00),
            ElaraColor(0.45, 0.55, 0.85),
            ElaraColor(0.05, 0.05, 0.08)
        )
    );
}

}
