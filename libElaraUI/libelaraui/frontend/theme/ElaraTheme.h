#ifndef ELARA_THEME_H
#define ELARA_THEME_H

#include <libelaracore/memory/String.h>

#include "ElaraPalette.h"

namespace elara {

class ElaraThemeProfile {
private:
    String name;
    ElaraPalette palette;

public:
    ElaraThemeProfile();
    ElaraThemeProfile(const String& profile_name);

    void setName(const String& profile_name);
    String getName() const;

    ElaraPalette* getPalette();
    const ElaraPalette* getPalette() const;
};

class ElaraTheme {
private:
    ElaraThemeProfile dark_profile;
    ElaraThemeProfile light_profile;
    ElaraThemeProfile* active_profile;

    void buildDark();
    void buildLight();

public:
    ElaraTheme();

    bool setMode(const String& mode);
    String getMode() const;

    ElaraPalette* getPalette();
    const ElaraPalette* getPalette() const;

    ElaraThemeProfile* getDarkProfile();
    ElaraThemeProfile* getLightProfile();
};

}

#endif
