#ifndef ELARA_DENSITY_MAP_WIDGET_H
#define ELARA_DENSITY_MAP_WIDGET_H

#include "../ElaraCanvasWidget.h"
#include <libelaracore/memory/Array.h>
#include <libelaracore/memory/Ref.h>

namespace elara {

class ElaraDensityMapLayer {
private:
    unsigned long long layer_capacity;
    unsigned int* bucket_counts;

public:
    ElaraDensityMapLayer();
    ElaraDensityMapLayer(unsigned long long capacity);
    virtual ~ElaraDensityMapLayer();

    void configure(unsigned long long capacity);
    void clear();

    unsigned long long capacity() const;
    unsigned int bucketCount(unsigned long long bucket) const;
    void add(unsigned long long bucket, unsigned int amount = 1);
};

class ElaraDensityMapWidget : public ElaraCanvasWidget {
private:
    Array< Ref<ElaraDensityMapLayer> > layers;

    unsigned long long sample_count;
    int gradient_steps;
    bool layer_zero_at_bottom;
    bool show_layer_lines;
    String overlay_text;

    int visualRowToLayer(int visual_row) const;
    double clamp01(double value) const;
    void colorForDensity(double density, double* r, double* g, double* b) const;

    double densityForPixel(
        int layer_index,
        int pixel_x,
        int pixel_width
    ) const;

    void drawLayer(
        ElaraDrawContext* ctx,
        int layer_index,
        double y,
        double h,
        int pixel_width
    );

public:
    ElaraDensityMapWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraDensityMapWidget();

    void clearLayers();
    void addLayer(unsigned long long capacity);
    void setPowerProfile(
        unsigned long long base_capacity,
        unsigned long long multiplier,
        int count
    );

    void setDefaultEightPowerLayers();
    void setDefaultBinarySixteenLayers();

    int layerCount() const;
    unsigned long long layerCapacity(int index) const;

    void clearSamples();
    void addSample(
        int layer_index,
        unsigned long long value_index,
        unsigned int amount = 1
    );

    void generateModuloSequence(
        unsigned long long sequence_sample_count,
        unsigned long long layer_value_multiplier
    );

    void setSampleCount(unsigned long long count);
    unsigned long long getSampleCount() const;

    void setGradientSteps(int steps);
    int getGradientSteps() const;

    void setLayerZeroAtBottom(bool value);
    bool isLayerZeroAtBottom() const;

    void setShowLayerLines(bool value);
    bool getShowLayerLines() const;

    void setOverlayText(const String& text);
    String getOverlayText() const;

protected:
    void drawCanvas(ElaraDrawContext* ctx);
};

}

#endif
