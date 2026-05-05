#ifndef ELARA_MULTI_AXIS_LINE_CHART_WIDGET_H
#define ELARA_MULTI_AXIS_LINE_CHART_WIDGET_H

#include "../ElaraCanvasWidget.h"
#include <libelaracore/memory/Array.h>

namespace elara {

class ElaraChartAxis {
public:
    String id;
    String label;
    String side;
    double minimum;
    double maximum;
    double r;
    double g;
    double b;

    ElaraChartAxis();
};

class ElaraChartSeries {
public:
    String id;
    String label;
    String axis_id;
    double r;
    double g;
    double b;
    Array<double> values;

    ElaraChartSeries();
};

class ElaraMultiAxisLineChartWidget : public ElaraCanvasWidget {
private:
    Array<ElaraChartAxis> axes;
    Array<ElaraChartSeries> series_list;
    String title;
    bool show_points;

    int findAxisIndex(const String& axis_id) const;
    double axisRange(const ElaraChartAxis& axis) const;
    double axisValueToY(
        const ElaraChartAxis& axis,
        double value,
        double plot_y,
        double plot_height
    ) const;

    void drawFrame(
        ElaraDrawContext* ctx,
        double plot_x,
        double plot_y,
        double plot_width,
        double plot_height
    );

    void drawAxes(
        ElaraDrawContext* ctx,
        double plot_x,
        double plot_y,
        double plot_width,
        double plot_height
    );

    void drawSeries(
        ElaraDrawContext* ctx,
        double plot_x,
        double plot_y,
        double plot_width,
        double plot_height
    );

    void drawLegend(ElaraDrawContext* ctx);

public:
    ElaraMultiAxisLineChartWidget(
        ElaraWidgetRegister* root_widget,
        ElaraWidgetHandle widget_handle
    );

    virtual ~ElaraMultiAxisLineChartWidget();

    void setTitle(const String& chart_title);
    String getTitle() const;

    void setShowPoints(bool value);
    bool getShowPoints() const;

    void clearAxes();
    void addAxis(const ElaraChartAxis& axis);
    int axisCount() const;
    ElaraChartAxis getAxis(int index) const;

    void clearSeries();
    void addSeries(const ElaraChartSeries& series);
    int seriesCount() const;
    ElaraChartSeries getSeries(int index) const;

protected:
    void drawCanvas(ElaraDrawContext* ctx);
};

}

#endif
