/**
 * @file RangeSlider.h
 * @brief 双滑块范围选择器（参考 depthsense scanner_ui RangeSlider）
 * 竖直方向：上为最大值、下为最小值；拖动可调节深度范围
 */
#ifndef SCANNER_VIEWER_RANGE_SLIDER_H
#define SCANNER_VIEWER_RANGE_SLIDER_H

#include <QSlider>
#include <QStyle>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QApplication>
#include <QtGlobal>

class RangeSlider : public QSlider {
    Q_OBJECT
public:
    explicit RangeSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
        : QSlider(orientation) {
        lowLimit = minimum();
        highLimit = maximum();
        pressed_control = QStyle::SC_None;
        tick_interval = 0;
        tick_position = QSlider::NoTicks;
        hover_control = QStyle::SC_None;
        click_offset = 0;
        active_slider = 0;
    }
    int low() const { return lowLimit; }
    void setLow(int low_limit) { lowLimit = low_limit; }
    int high() const { return highLimit; }
    void setHigh(int high_limit) { highLimit = high_limit; }

signals:
    void sliderMoved(int low, int high);

protected:
    void paintEvent(QPaintEvent* ev) override {
        Q_UNUSED(ev);
        QPainter painter(this);
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        opt.sliderValue = 0;
        opt.sliderPosition = 0;
        opt.subControls = QStyle::SC_SliderGroove;
        if (tickPosition() != NoTicks) opt.subControls |= QStyle::SC_SliderTickmarks;
        style()->drawComplexControl(QStyle::CC_Slider, &opt, &painter, this);
        QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

        initStyleOption(&opt);
        opt.subControls = QStyle::SC_SliderGroove;
        opt.sliderValue = 0;
        opt.sliderPosition = lowLimit;
        QRect low_rect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
        opt.sliderPosition = highLimit;
        QRect high_rect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

        int low_pos = pick(low_rect.center());
        int high_pos = pick(high_rect.center());
        int min_pos = qMin(low_pos, high_pos);
        int max_pos = qMax(low_pos, high_pos);
        QPoint c = QRect(low_rect.center(), high_rect.center()).center();
        QRect span_rect;
        if (opt.orientation == Qt::Horizontal) {
            span_rect = QRect(QPoint(min_pos, c.y() - 2), QPoint(max_pos, c.y() + 1));
            groove.adjust(0, 0, -1, 0);
        } else {
            span_rect = QRect(QPoint(c.x() - 2, min_pos), QPoint(c.x() + 2, max_pos));
            groove.adjust(0, 0, 0, -1);
        }
        QColor highlight("#6b5382");
        painter.setBrush(QBrush(highlight));
        painter.setPen(QPen(highlight, 0));
        painter.drawRect(span_rect.intersected(groove));

        QStyle* s = style();
        initStyleOption(&opt);
        opt.subControls = QStyle::SC_SliderHandle;
        if (tickPosition() != QSlider::NoTicks) opt.subControls |= QStyle::SC_SliderTickmarks;
        if (pressed_control) opt.activeSubControls = pressed_control;
        else opt.activeSubControls = hover_control;
        opt.sliderPosition = lowLimit;
        opt.sliderValue = lowLimit;
        s->drawComplexControl(QStyle::CC_Slider, &opt, &painter, this);
        opt.sliderPosition = highLimit;
        opt.sliderValue = highLimit;
        s->drawComplexControl(QStyle::CC_Slider, &opt, &painter, this);
    }

    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton) {
            ev->accept();
            QStyleOptionSlider opt;
            initStyleOption(&opt);
            active_slider = -1;
            pressed_control = QStyle::SC_None;
            // 用子控件矩形自己判断点中哪个柄，避免样式 hitTest 对双柄不敏感
            opt.sliderPosition = lowLimit;
            opt.sliderValue = lowLimit;
            QRect low_rect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            opt.sliderPosition = highLimit;
            opt.sliderValue = highLimit;
            QRect high_rect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            // 扩大可点区域，便于窄滑块点击（在柄的垂直方向各扩展一点）
            const int expand = 4;
            if (orientation() == Qt::Vertical) {
                low_rect.adjust(-expand, -expand, expand, expand);
                high_rect.adjust(-expand, -expand, expand, expand);
            } else {
                low_rect.adjust(-expand, -expand, expand, expand);
                high_rect.adjust(-expand, -expand, expand, expand);
            }
            bool in_low = low_rect.contains(ev->pos());
            bool in_high = high_rect.contains(ev->pos());
            if (in_low && in_high) {
                // 两柄重叠时选离点击位置更近的
                int dist_low = qAbs(pick(ev->pos()) - pick(low_rect.center()));
                int dist_high = qAbs(pick(ev->pos()) - pick(high_rect.center()));
                active_slider = (dist_low <= dist_high) ? 0 : 1;
            } else if (in_low) {
                active_slider = 0;
            } else if (in_high) {
                active_slider = 1;
            } else {
                active_slider = -1;
                click_offset = pixelPosToRangeValue(pick(ev->pos()));
            }
            if (active_slider >= 0) {
                pressed_control = QStyle::SC_SliderHandle;
                setSliderDown(true);
            } else {
                pressed_control = QStyle::SC_SliderHandle;
            }
            triggerAction(SliderMove);
            setRepeatAction(SliderNoAction);
        } else {
            ev->ignore();
        }
        QSlider::mousePressEvent(ev);
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        if (pressed_control != QStyle::SC_SliderHandle) {
            ev->ignore();
            return;
        }
        ev->accept();
        int new_pos = pixelPosToRangeValue(pick(ev->pos()));
        int diff;
        if (active_slider < 0) {
            int offset = new_pos - click_offset;
            highLimit += offset;
            lowLimit += offset;
            if (lowLimit < minimum()) {
                diff = minimum() - lowLimit;
                lowLimit += diff;
                highLimit += diff;
            }
            if (highLimit > maximum()) {
                diff = maximum() - highLimit;
                lowLimit += diff;
                highLimit += diff;
            }
        } else if (active_slider == 0) {
            if (new_pos >= highLimit) new_pos = highLimit - 1;
            lowLimit = new_pos;
        } else {
            if (new_pos <= lowLimit) new_pos = lowLimit + 1;
            highLimit = new_pos;
        }
        click_offset = new_pos;
        update();
        emit sliderMoved(lowLimit, highLimit);
    }

private:
    int lowLimit, highLimit;
    int pick(const QPoint& pt) const { return orientation() == Qt::Horizontal ? pt.x() : pt.y(); }
    int pixelPosToRangeValue(int pos) {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        QRect gr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        QRect sr = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
        int slider_length, slider_min, slider_max;
        if (orientation() == Qt::Horizontal) {
            slider_length = sr.width();
            slider_min = gr.x();
            slider_max = gr.right() - slider_length + 1;
        } else {
            slider_length = sr.height();
            slider_min = gr.y();
            slider_max = gr.bottom() - slider_length + 1;
        }
        return style()->sliderValueFromPosition(minimum(), maximum(), pos - slider_min, slider_max - slider_min, opt.upsideDown);
    }
    QStyle::SubControl pressed_control;
    int tick_interval;
    QSlider::TickPosition tick_position;
    QStyle::SubControl hover_control;
    int click_offset;
    int active_slider;
};

#endif
