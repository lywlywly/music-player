#pragma once
#include <QApplication>
#include <QMouseEvent>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>

// based on
// https://github.com/strawberrymusicplayer/strawberry/blob/master/src/widgets/sliderslider.cpp
class JumpSlider : public QSlider {
  Q_OBJECT
public:
  using QSlider::QSlider;

protected:
  bool sliding_ = false;
  bool outside_ = false;
  int prev_value_ = 0;

  void mousePressEvent(QMouseEvent *e) override {
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt,
                                               QStyle::SC_SliderHandle, this);

    sliding_ = true;
    prev_value_ = value();

    if (!handleRect.contains(e->pos()))
      slideToMouse(e);

    QSlider::mousePressEvent(e);
  }

  void mouseMoveEvent(QMouseEvent *e) override {
    if (sliding_) {
      QRect active(-20, -20, width() + 40, height() + 40);

      if (!active.contains(e->pos())) {
        if (!outside_)
          setValue(prev_value_);
        outside_ = true;
      } else {
        outside_ = false;
        slideToMouse(e);
        emit sliderMoved(value());
      }
    } else {
      QSlider::mouseMoveEvent(e);
    }
  }

  void mouseReleaseEvent(QMouseEvent *e) override {
    if (!outside_ && value() != prev_value_)
      emit sliderMoved(value());

    sliding_ = false;
    outside_ = false;

    QSlider::mouseReleaseEvent(e);
  }

private:
  void slideToMouse(QMouseEvent *e) {
    QStyleOptionSlider opt;
    initStyleOption(&opt);

    QRect handleRect = style()->subControlRect(QStyle::CC_Slider, &opt,
                                               QStyle::SC_SliderHandle, this);

    int pos = (orientation() == Qt::Horizontal)
                  ? e->pos().x() - handleRect.width() / 2
                  : e->pos().y() - handleRect.height() / 2;

    int maxPos = (orientation() == Qt::Horizontal)
                     ? width() - handleRect.width()
                     : height() - handleRect.height();

    pos = std::clamp(pos, 0, maxPos);

    bool rtl = (orientation() == Qt::Horizontal &&
                QApplication::layoutDirection() == Qt::RightToLeft);

    int newVal = QStyle::sliderValueFromPosition(minimum(), maximum(),
                                                 rtl ? (maxPos - pos) : pos,
                                                 maxPos, opt.upsideDown);

    QSlider::setValue(newVal);
  }
};
