#include "ElaraMultiAxisLineChartWidget.h"

namespace elara {

ElaraChartAxis::ElaraChartAxis()
    : side("left"),
      minimum(0),
      maximum(100),
      r(0.20),
      g(0.50),
      b(0.90) {
}

ElaraChartSeries::ElaraChartSeries()
    : r(0.15),
      g(0.65),
      b(0.95),
      values() {
}

ElaraMultiAxisLineChartWidget::ElaraMultiAxisLineChartWidget(
    ElaraWidgetRegister* root_widget,
    ElaraWidgetHandle widget_handle
) : ElaraCanvasWidget(root_widget, widget_handle),
    axes(),
    series_list(),
    title("Multi-axis line chart"),
    show_points(true) {
}

ElaraMultiAxisLineChartWidget::~ElaraMultiAxisLineChartWidget() {
}

void ElaraMultiAxisLineChartWidget::setTitle(const String& chart_title) {
    title = chart_title;
}

String ElaraMultiAxisLineChartWidget::getTitle() const {
    return title;
}

void ElaraMultiAxisLineChartWidget::setShowPoints(bool value) {
    show_points = value;
}

bool ElaraMultiAxisLineChartWidget::getShowPoints() const {
    return show_points;
}

void ElaraMultiAxisLineChartWidget::clearAxes() {
    axes.clear();
}

void ElaraMultiAxisLineChartWidget::addAxis(const ElaraChartAxis& axis) {
    axes.push(axis);
}

int ElaraMultiAxisLineChartWidget::axisCount() const {
    return (int)axes.length();
}

ElaraChartAxis ElaraMultiAxisLineChartWidget::getAxis(int index) const {
    if(index < 0 || index >= (int)axes.length()) {
        return ElaraChartAxis();
    }

    return axes[index];
}

void ElaraMultiAxisLineChartWidget::clearSeries() {
    series_list.clear();
}

void ElaraMultiAxisLineChartWidget::addSeries(const ElaraChartSeries& series) {
    series_list.push(series);
}

int ElaraMultiAxisLineChartWidget::seriesCount() const {
    return (int)series_list.length();
}

ElaraChartSeries ElaraMultiAxisLineChartWidget::getSeries(int index) const {
    if(index < 0 || index >= (int)series_list.length()) {
        return ElaraChartSeries();
    }

    return series_list[index];
}

int ElaraMultiAxisLineChartWidget::findAxisIndex(const String& axis_id) const {
    for(int i = 0; i < (int)axes.length(); i++) {
        if(axes[i].id == axis_id) {
            return i;
        }
    }

    return -1;
}

double ElaraMultiAxisLineChartWidget::axisRange(const ElaraChartAxis& axis) const {
    double range = axis.maximum - axis.minimum;

    if(range <= 0) {
        return 1.0;
    }

    return range;
}

double ElaraMultiAxisLineChartWidget::axisValueToY(
    const ElaraChartAxis& axis,
    double value,
    double plot_y,
    double plot_height
) const {
    double t = (value - axis.minimum) / axisRange(axis);

    if(t < 0) {
        t = 0;
    }

    if(t > 1) {
        t = 1;
    }

    return plot_y + plot_height - (t * plot_height);
}

void ElaraMultiAxisLineChartWidget::drawFrame(
    ElaraDrawContext* ctx,
    double plot_x,
    double plot_y,
    double plot_width,
    double plot_height
) {
    ElaraPaletteTriplet panel = colors("panel", "default");
    ElaraPaletteTriplet input = colors("input", "default");

    ctx->setColor(panel.base.r, panel.base.g, panel.base.b);
    ctx->fillRect(plot_x, plot_y, plot_width, plot_height);

    ctx->setColor(input.accent.r, input.accent.g, input.accent.b);
    ctx->line(plot_x, plot_y, plot_x + plot_width, plot_y, 1);
    ctx->line(plot_x, plot_y + plot_height, plot_x + plot_width, plot_y + plot_height, 1);
    ctx->line(plot_x, plot_y, plot_x, plot_y + plot_height, 1);
    ctx->line(plot_x + plot_width, plot_y, plot_x + plot_width, plot_y + plot_height, 1);

    ctx->setColor(input.accent.r * 0.85, input.accent.g * 0.85, input.accent.b * 0.85);
    for(int i = 1; i < 5; i++) {
        double gy = plot_y + ((plot_height / 5.0) * (double)i);
        ctx->line(plot_x, gy, plot_x + plot_width, gy, 1);
    }
}

void ElaraMultiAxisLineChartWidget::drawAxes(
    ElaraDrawContext* ctx,
    double plot_x,
    double plot_y,
    double plot_width,
    double plot_height
) {
    ElaraPaletteTriplet panel = colors("panel", "default");
    ctx->setColor(panel.text.r, panel.text.g, panel.text.b);

    if(title.length() > 0) {
        ctx->drawText(plot_x, 22, title, 15);
    }

    if(axes.length() <= 0) {
        ctx->drawText(plot_x, plot_y + plot_height + 24, "No axes configured", 12);
        return;
    }

    for(int i = 0; i < (int)axes.length(); i++) {
        const ElaraChartAxis& axis = axes[i];
        bool right_side = axis.side == String("right");
        double label_x = right_side ? plot_x + plot_width + 10 : 8;

        ctx->setColor(axis.r, axis.g, axis.b);
        ctx->drawText(label_x, plot_y + 14, axis.label, 12);
        ctx->drawText(label_x, plot_y + 30, String(axis.maximum), 11);
        ctx->drawText(label_x, plot_y + plot_height - 4, String(axis.minimum), 11);
    }

    ctx->setColor(panel.text.r, panel.text.g, panel.text.b);
    ctx->drawText(plot_x, plot_y + plot_height + 24, "sample index", 12);
}

void ElaraMultiAxisLineChartWidget::drawSeries(
    ElaraDrawContext* ctx,
    double plot_x,
    double plot_y,
    double plot_width,
    double plot_height
) {
    for(int series_index = 0; series_index < (int)series_list.length(); series_index++) {
        const ElaraChartSeries& series = series_list[series_index];
        int axis_index = findAxisIndex(series.axis_id);

        if(axis_index < 0 || axis_index >= (int)axes.length()) {
            continue;
        }

        const ElaraChartAxis& axis = axes[axis_index];
        int count = (int)series.values.length();

        if(count <= 0) {
            continue;
        }

        ctx->setColor(series.r, series.g, series.b);

        for(int i = 0; i < count; i++) {
            double px;

            if(count <= 1) {
                px = plot_x + (plot_width / 2.0);
            } else {
                px = plot_x + (((double)i / (double)(count - 1)) * plot_width);
            }

            double py = axisValueToY(axis, series.values[i], plot_y, plot_height);

            if(i > 0) {
                double prev_x;

                if(count <= 1) {
                    prev_x = px;
                } else {
                    prev_x = plot_x + (((double)(i - 1) / (double)(count - 1)) * plot_width);
                }

                double prev_y = axisValueToY(axis, series.values[i - 1], plot_y, plot_height);
                ctx->line(prev_x, prev_y, px, py, 2);
            }

            if(show_points) {
                ctx->fillCircle(px, py, 3);
            }
        }
    }
}

void ElaraMultiAxisLineChartWidget::drawLegend(ElaraDrawContext* ctx) {
    double legend_x = 12;
    double legend_y = height - 14;

    for(int i = 0; i < (int)series_list.length(); i++) {
        const ElaraChartSeries& series = series_list[i];
        ctx->setColor(series.r, series.g, series.b);
        ctx->line(legend_x, legend_y - 4, legend_x + 16, legend_y - 4, 2);
        ctx->fillCircle(legend_x + 8, legend_y - 4, 3);
        ctx->drawText(legend_x + 22, legend_y, series.label, 11);
        legend_x += 120;
    }
}

void ElaraMultiAxisLineChartWidget::drawCanvas(ElaraDrawContext* ctx) {
    double left_margin = 66;
    double right_margin = 78;
    double top_margin = 34;
    double bottom_margin = 44;
    double plot_x = left_margin;
    double plot_y = top_margin;
    double plot_width = width - left_margin - right_margin;
    double plot_height = height - top_margin - bottom_margin;

    if(plot_width < 40 || plot_height < 40) {
        return;
    }

    drawFrame(ctx, plot_x, plot_y, plot_width, plot_height);
    drawAxes(ctx, plot_x, plot_y, plot_width, plot_height);
    drawSeries(ctx, plot_x, plot_y, plot_width, plot_height);
    drawLegend(ctx);
}

}
