/*
This file is part of Bettergram.

For license and copyright information please follow this link:
https://github.com/bettergram/bettergram/blob/master/LEGAL
*/
#include "ui/widgets/discrete_sliders.h"

#include "ui/effects/ripple_animation.h"
#include "styles/style_widgets.h"

namespace Ui {

DiscreteSlider::DiscreteSlider(QWidget *parent) : RpWidget(parent) {
	setCursor(style::cur_pointer);
}

void DiscreteSlider::setActiveSection(int index) {
	if (_activeIndex != index) {
		_activeIndex = index;
		activateCallback();
	}
	setSelectedSection(index);
}

void DiscreteSlider::activateCallback() {
	if (_timerId >= 0) {
		killTimer(_timerId);
		_timerId = -1;
	}
	auto ms = crl::now();
	if (ms >= _callbackAfterMs) {
		_sectionActivated.fire_copy(_activeIndex);
	} else {
		_timerId = startTimer(_callbackAfterMs - ms, Qt::PreciseTimer);
	}
}

void DiscreteSlider::timerEvent(QTimerEvent *e) {
	activateCallback();
}

void DiscreteSlider::setActiveSectionFast(int index) {
	setActiveSection(index);
	finishAnimating();
}

void DiscreteSlider::finishAnimating() {
	_a_left.finish();
	update();
}

void DiscreteSlider::setSelectOnPress(bool selectOnPress) {
	_selectOnPress = selectOnPress;
}

void DiscreteSlider::addSection(const QString &label) {
	_sections.push_back(Section(label, getLabelFont()));
	resizeToWidth(width());
}

void DiscreteSlider::setSections(const QStringList &labels) {
	Assert(!labels.isEmpty());

	_sections.clear();
	for_const (auto &label, labels) {
		_sections.push_back(Section(label, getLabelFont()));
	}
	stopAnimation();
	if (_activeIndex >= _sections.size()) {
		_activeIndex = 0;
	}
	if (_selected >= _sections.size()) {
		_selected = 0;
	}
	resizeToWidth(width());
}

int DiscreteSlider::getCurrentActiveLeft(crl::time ms) {
	const auto left = _sections.empty() ? 0 : _sections[_selected].left;
	return _a_left.current(ms, left);
}

int DiscreteSlider::getCurrentActiveWidth() const {
	return _sections.empty() ? 0 : _sections[_selected].width;
}

template <typename Lambda>
void DiscreteSlider::enumerateSections(Lambda callback) {
	for (auto &section : _sections) {
		if (!callback(section)) {
			return;
		}
	}
}

template <typename Lambda>
void DiscreteSlider::enumerateSections(Lambda callback) const {
	for (auto &section : _sections) {
		if (!callback(section)) {
			return;
		}
	}
}

void DiscreteSlider::mousePressEvent(QMouseEvent *e) {
	auto index = getIndexFromPosition(e->pos());
	if (_selectOnPress) {
		setSelectedSection(index);
	}
	startRipple(index);
	_pressed = index;
}

void DiscreteSlider::mouseMoveEvent(QMouseEvent *e) {
	if (_pressed < 0) return;
	if (_selectOnPress) {
		setSelectedSection(getIndexFromPosition(e->pos()));
	}
}

void DiscreteSlider::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0) return;

	auto index = getIndexFromPosition(e->pos());
	if (pressed < _sections.size()) {
		if (_sections[pressed].ripple) {
			_sections[pressed].ripple->lastStop();
		}
	}
	if (_selectOnPress || index == pressed) {
		setActiveSection(index);
	}
}

void DiscreteSlider::setSelectedSection(int index) {
	if (index < 0 || index >= _sections.size()) return;

	if (_selected != index) {
		auto from = _sections[_selected].left;
		_selected = index;
		auto to = _sections[_selected].left;
		auto duration = getAnimationDuration();
		_a_left.start([this] { update(); }, from, to, duration);
		_callbackAfterMs = crl::now() + duration;
	}
}

int DiscreteSlider::getIndexFromPosition(QPoint pos) {
	int count = _sections.size();
	for (int i = 0; i != count; ++i) {
		if (_sections[i].left + _sections[i].width > pos.x()) {
			return i;
		}
	}
	return count - 1;
}

DiscreteSlider::Section::Section(const QString &label, const style::font &font)
: label(label)
, labelWidth(font->width(label)) {
}

SettingsSlider::SettingsSlider(QWidget *parent, const style::SettingsSlider &st) : DiscreteSlider(parent)
, _st(st) {
	setSelectOnPress(_st.ripple.showDuration == 0);
}

void SettingsSlider::setRippleTopRoundRadius(int radius) {
	_rippleTopRoundRadius = radius;
}

const style::font &SettingsSlider::getLabelFont() const {
	return _st.labelFont;
}

int SettingsSlider::getAnimationDuration() const {
	return _st.duration;
}

void SettingsSlider::resizeSections(int newWidth) {
	auto count = getSectionsCount();
	if (!count) return;

	auto sectionWidths = countSectionsWidths(newWidth);

	auto skip = 0;
	auto x = 0.;
	auto sectionWidth = sectionWidths.begin();
	enumerateSections([&](Section &section) {
		Expects(sectionWidth != sectionWidths.end());

		section.left = qFloor(x) + skip;
		x += *sectionWidth;
		section.width = qRound(x) - (section.left - skip);
		skip += _st.barSkip;
		++sectionWidth;
		return true;
	});
	stopAnimation();
}

std::vector<float64> SettingsSlider::countSectionsWidths(
		int newWidth) const {
	auto count = getSectionsCount();
	auto sectionsWidth = newWidth - (count - 1) * _st.barSkip;
	auto sectionWidth = sectionsWidth / float64(count);

	auto result = std::vector<float64>(count, sectionWidth);
	auto labelsWidth = 0;
	auto commonWidth = true;
	enumerateSections([&](const Section &section) {
		labelsWidth += section.labelWidth;
		if (section.labelWidth >= sectionWidth) {
			commonWidth = false;
		}
		return true;
	});
	// If labelsWidth > sectionsWidth we're screwed anyway.
	if (!commonWidth && labelsWidth <= sectionsWidth) {
		auto padding = (sectionsWidth - labelsWidth) / (2. * count);
		auto currentWidth = result.begin();
		enumerateSections([&](const Section &section) {
			Expects(currentWidth != result.end());

			*currentWidth = padding + section.labelWidth + padding;
			++currentWidth;
			return true;
		});
	}
	return result;
}

int SettingsSlider::resizeGetHeight(int newWidth) {
	resizeSections(newWidth);
	return _st.height;
}

void SettingsSlider::startRipple(int sectionIndex) {
	if (!_st.ripple.showDuration) return;
	auto index = 0;
	enumerateSections([this, &index, sectionIndex](Section &section) {
		if (index++ == sectionIndex) {
			if (!section.ripple) {
				auto mask = prepareRippleMask(sectionIndex, section);
				section.ripple = std::make_unique<RippleAnimation>(
					_st.ripple,
					std::move(mask),
					[this] { update(); });
			}
			const auto point = mapFromGlobal(QCursor::pos());
			section.ripple->add(point - QPoint(section.left, 0));
			return false;
		}
		return true;
	});
}

QImage SettingsSlider::prepareRippleMask(int sectionIndex, const Section &section) {
	auto size = QSize(section.width, height() - _st.rippleBottomSkip);
	if (!_rippleTopRoundRadius || (sectionIndex > 0 && sectionIndex + 1 < getSectionsCount())) {
		return RippleAnimation::rectMask(size);
	}
	return RippleAnimation::maskByDrawer(size, false, [this, sectionIndex, width = section.width](QPainter &p) {
		auto plusRadius = _rippleTopRoundRadius + 1;
		p.drawRoundedRect(0, 0, width, height() + plusRadius, _rippleTopRoundRadius, _rippleTopRoundRadius);
		if (sectionIndex > 0) {
			p.fillRect(0, 0, plusRadius, plusRadius, p.brush());
		}
		if (sectionIndex + 1 < getSectionsCount()) {
			p.fillRect(width - plusRadius, 0, plusRadius, plusRadius, p.brush());
		}
	});
}

void SettingsSlider::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	auto ms = crl::now();
	auto activeLeft = getCurrentActiveLeft(ms);
	auto activeWidth = getCurrentActiveWidth();

	p.setFont(_st.labelFont);
	enumerateSections([this, &p, activeLeft, activeWidth, ms, clip](Section &section) {
		auto active = 1. - snap(qAbs(activeLeft - section.left) / float64(section.width), 0., 1.);
		if (section.ripple) {
			auto color = anim::color(_st.rippleBg, _st.rippleBgActive, active);
			section.ripple->paint(p, section.left, 0, width(), ms, &color);
			if (section.ripple->empty()) {
				section.ripple.reset();
			}
		}
		auto from = section.left, tofill = section.width;
		if (activeLeft > from) {
			auto fill = qMin(tofill, activeLeft - from);
			p.fillRect(myrtlrect(from, _st.barTop, fill, _st.barStroke), _st.barFg);
			from += fill;
			tofill -= fill;
		}
		if ((activeLeft >= section.left) || (activeLeft + activeWidth > from)) {
			if (auto fill = qMin(tofill, activeLeft + activeWidth - from)) {
				p.fillRect(myrtlrect(from, _st.barTop, fill, _st.barStroke), _st.barFgActive);
				from += fill;
				tofill -= fill;
			}
		}
		if (tofill) {
			p.fillRect(myrtlrect(from, _st.barTop, tofill, _st.barStroke), _st.barFg);
		}
		if (myrtlrect(section.left, _st.labelTop, section.width, _st.labelFont->height).intersects(clip)) {
			p.setPen(anim::pen(_st.labelFg, _st.labelFgActive, active));
			p.drawTextLeft(section.left + (section.width - section.labelWidth) / 2, _st.labelTop, width(), section.label, section.labelWidth);
		}
		return true;
	});
}

} // namespace Ui
