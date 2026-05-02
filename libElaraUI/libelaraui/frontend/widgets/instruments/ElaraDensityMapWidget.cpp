#include "ElaraDensityMapWidget.h"

#include <string.h>

namespace elara {

ElaraDensityMapLayer::ElaraDensityMapLayer()
    : layer_capacity(0),
      bucket_counts(0) {}

ElaraDensityMapLayer::ElaraDensityMapLayer(unsigned long long capacity)
    : layer_capacity(0),
      bucket_counts(0) {
    configure(capacity);
}

ElaraDensityMapLayer::~ElaraDensityMapLayer() {
    if(bucket_counts) {
        delete[] bucket_counts;
        bucket_counts = 0;
    }
}

void ElaraDensityMapLayer::configure(unsigned long long capacity_value) {
    if(bucket_counts) {
        delete[] bucket_counts;
        bucket_counts = 0;
    }

    layer_capacity = capacity_value;

    if(layer_capacity > 0) {
        bucket_counts = new unsigned int[(size_t)layer_capacity];
        clear();
    }
}

void ElaraDensityMapLayer::clear() {
    if(bucket_counts && layer_capacity > 0) {
        memset(bucket_counts, 0, sizeof(unsigned int) * (size_t)layer_capacity);
    }
}

unsigned long long ElaraDensityMapLayer::capacity() const {
    return layer_capacity;
}

unsigned int ElaraDensityMapLayer::bucketCount(unsigned long long bucket) const {
    if(!bucket_counts || bucket >= layer_capacity) {
        return 0;
    }

    return bucket_counts[(size_t)bucket];
}

void ElaraDensityMapLayer::add(unsigned long long bucket, unsigned int amount) {
    if(!bucket_counts || layer_capacity == 0) {
        return;
    }

    bucket = bucket % layer_capacity;
    bucket_counts[(size_t)bucket] += amount;
}

ElaraDensityMapWidget::ElaraDensityMapWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraCanvasWidget(root_widget, widget_handle),
    sample_count(0),
    gradient_steps(256),
    layer_zero_at_bottom(true),
    show_layer_lines(true),
    overlay_text("") {
    setDefaultBinarySixteenLayers();
}

ElaraDensityMapWidget::~ElaraDensityMapWidget() {}

void ElaraDensityMapWidget::clearLayers() {
    layers.clear();
    sample_count = 0;
}

void ElaraDensityMapWidget::addLayer(unsigned long long capacity) {
    layers.push(Ref<ElaraDensityMapLayer>(new ElaraDensityMapLayer(capacity)));
}

void ElaraDensityMapWidget::setPowerProfile(
    unsigned long long base_capacity,
    unsigned long long multiplier,
    int count
) {
    clearLayers();

    if(base_capacity < 1) {
        base_capacity = 1;
    }

    if(multiplier < 1) {
        multiplier = 1;
    }

    unsigned long long capacity = base_capacity;

    for(int i = 0; i < count; i++) {
        addLayer(capacity);
        capacity *= multiplier;
    }
}

void ElaraDensityMapWidget::setDefaultEightPowerLayers() {
    setPowerProfile(8ULL, 8ULL, 9);
}

void ElaraDensityMapWidget::setDefaultBinarySixteenLayers() {
    setPowerProfile(8ULL, 2ULL, 16);
}

int ElaraDensityMapWidget::layerCount() const {
    return (int)layers.length();
}

unsigned long long ElaraDensityMapWidget::layerCapacity(int index) const {
    if(index < 0 || index >= (int)layers.length() || !layers[index]) {
        return 0;
    }

    return layers[index]->capacity();
}

void ElaraDensityMapWidget::clearSamples() {
    for(int i = 0; i < (int)layers.length(); i++) {
        if(layers[i]) {
            layers[i]->clear();
        }
    }

    sample_count = 0;
}

void ElaraDensityMapWidget::addSample(
    int layer_index,
    unsigned long long value_index,
    unsigned int amount
) {
    if(layer_index < 0 || layer_index >= (int)layers.length() || !layers[layer_index]) {
        return;
    }

    layers[layer_index]->add(value_index, amount);
}

void ElaraDensityMapWidget::generateModuloSequence(
    unsigned long long sequence_sample_count,
    unsigned long long layer_value_multiplier
) {
    clearSamples();

    if(layer_value_multiplier < 1) {
        layer_value_multiplier = 1;
    }

    sample_count = sequence_sample_count;

    for(unsigned long long starting_sample = 0;
        starting_sample < sequence_sample_count;
        starting_sample++) {

        unsigned long long layer_factor = 1;

        for(int layer = 0; layer < (int)layers.length(); layer++) {
            unsigned long long capacity = layerCapacity(layer);

            if(capacity > 0) {
                unsigned long long bucket = (starting_sample * layer_factor) % capacity;
                addSample(layer, bucket);
            }

            layer_factor *= layer_value_multiplier;
        }
    }
}

void ElaraDensityMapWidget::setSampleCount(unsigned long long count) {
    sample_count = count;
}

unsigned long long ElaraDensityMapWidget::getSampleCount() const {
    return sample_count;
}

void ElaraDensityMapWidget::setGradientSteps(int steps) {
    gradient_steps = steps;

    if(gradient_steps < 2) {
        gradient_steps = 2;
    }
}

int ElaraDensityMapWidget::getGradientSteps() const {
    return gradient_steps;
}

void ElaraDensityMapWidget::setLayerZeroAtBottom(bool value) {
    layer_zero_at_bottom = value;
}

bool ElaraDensityMapWidget::isLayerZeroAtBottom() const {
    return layer_zero_at_bottom;
}

void ElaraDensityMapWidget::setShowLayerLines(bool value) {
    show_layer_lines = value;
}

bool ElaraDensityMapWidget::getShowLayerLines() const {
    return show_layer_lines;
}

void ElaraDensityMapWidget::setOverlayText(const String& text) {
    overlay_text = text;
}

String ElaraDensityMapWidget::getOverlayText() const {
    return overlay_text;
}

int ElaraDensityMapWidget::visualRowToLayer(int visual_row) const {
    int count = (int)layers.length();

    if(layer_zero_at_bottom) {
        return count - visual_row - 1;
    }

    return visual_row;
}

double ElaraDensityMapWidget::clamp01(double value) const {
    if(value < 0) {
        return 0;
    }

    if(value > 1) {
        return 1;
    }

    return value;
}

void ElaraDensityMapWidget::colorForDensity(
    double density,
    double* r,
    double* g,
    double* b
) const {
    double t = clamp01(density);

    if(gradient_steps > 1) {
        double stepped = (double)((int)(t * (gradient_steps - 1) + 0.5));
        t = stepped / (double)(gradient_steps - 1);
    }

    if(t < 0.5) {
        double p = t / 0.5;
        *r = 1.0 - (0.65 * p);
        *g = 1.0 - (0.25 * p);
        *b = 1.0;
    } else {
        double p = (t - 0.5) / 0.5;
        *r = 0.35 + (0.45 * p);
        *g = 0.75 - (0.75 * p);
        *b = 1.0 - (1.0 * p);
    }
}

double ElaraDensityMapWidget::densityForPixel(
    int layer_index,
    int pixel_x,
    int pixel_width
) const {
    if(layer_index < 0 || layer_index >= (int)layers.length() || !layers[layer_index]) {
        return 0;
    }

    unsigned long long capacity = layerCapacity(layer_index);

    if(capacity == 0 || pixel_width <= 0) {
        return 0;
    }

    /*
        Density is address-space coverage, not bucket fill volume.

        A bucket hit 8192 times is not "8192 times redder" than a bucket hit once.
        It is simply present in the density field. The colour answers:

            "What fraction of the address range represented by this pixel
             has at least one sample?"

        When capacity <= pixel_width, each logical bucket is stretched across
        multiple pixels. Those pixels should all inherit that bucket's occupied
        or empty state.

        When capacity > pixel_width, each pixel represents many buckets, so the
        density is the occupied-bucket fraction inside that bucket range. Missing
        buckets are explicitly zero because they are included in bucket_total.
    */

    if(capacity <= (unsigned long long)pixel_width) {
        unsigned long long bucket =
            ((unsigned long long)pixel_x * capacity) /
            (unsigned long long)pixel_width;

        if(bucket >= capacity) {
            bucket = capacity - 1;
        }

        if(layers[layer_index]->bucketCount(bucket) > 0) {
            return 1.0;
        }

        return 0.0;
    }

    unsigned long long start =
        ((unsigned long long)pixel_x * capacity) /
        (unsigned long long)pixel_width;

    unsigned long long end =
        ((unsigned long long)(pixel_x + 1) * capacity) /
        (unsigned long long)pixel_width;

    if(end <= start) {
        end = start + 1;
    }

    if(end > capacity) {
        end = capacity;
    }

    unsigned long long bucket_total = 0;
    unsigned long long occupied_total = 0;

    for(unsigned long long bucket = start; bucket < end; bucket++) {
        bucket_total++;

        if(layers[layer_index]->bucketCount(bucket) > 0) {
            occupied_total++;
        }
    }

    if(bucket_total == 0) {
        return 0;
    }

    return (double)occupied_total / (double)bucket_total;
}

void ElaraDensityMapWidget::drawLayer(
    ElaraDrawContext* ctx,
    int layer_index,
    double y,
    double h,
    int pixel_width
) {
    if(pixel_width <= 0 || h <= 0) {
        return;
    }

    for(int px = 0; px < pixel_width; px++) {
        double density = densityForPixel(layer_index, px, pixel_width);
        double r = 1;
        double g = 1;
        double b = 1;

        colorForDensity(density, &r, &g, &b);
        ctx->setColor(r, g, b);
        ctx->fillRect(px, y, 1, h + 1);
    }
}

void ElaraDensityMapWidget::drawCanvas(ElaraDrawContext* ctx) {
    int pixel_width = (int)width;
    int count = (int)layers.length();

    if(pixel_width <= 0 || height <= 0 || count <= 0) {
        return;
    }

    double layer_h = height / (double)count;

    for(int visual_row = 0; visual_row < count; visual_row++) {
        int layer_index = visualRowToLayer(visual_row);
        double y = (double)visual_row * layer_h;
        drawLayer(ctx, layer_index, y, layer_h, pixel_width);
    }

    if(show_layer_lines) {
        ctx->setColor(0.0, 0.0, 0.0);

        for(int visual_row = 1; visual_row < count; visual_row++) {
            double y = (double)visual_row * layer_h;
            ctx->line(0, y, width, y, 1);
        }
    }

    if(overlay_text.length() > 0) {
        ctx->setColor(0.0, 0.0, 0.0);
        ctx->drawText(12, 22, overlay_text, 14);
    }
}

}
