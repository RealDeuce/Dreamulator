// license:BSD-3-Clause
// copyright-holders:Stephen Hurd
// HP PCL command parser for DreamWriter JET printer model
#include "printer.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class PclPrinter : public PrinterSim {
public:
	using PrinterSim::PrinterSim;

protected:
	void parse_byte(uint8_t b) override;

private:
	void process_parameterized();
	void apply_param(char group, char sub_group, float value, char terminator);

	enum class State { Normal, Esc, TwoChar, Parameterized };
	State state_ = State::Normal;
	char group_ = 0;
	char sub_group_ = 0;
	char param_buf_[64] = {};
	int param_pos_ = 0;
};

void PclPrinter::parse_byte(uint8_t b)
{
	switch (state_) {
	case State::Normal:
		switch (b) {
		case 0x1B: state_ = State::Esc; return;
		case 0x0D: carriage_return(); return;
		case 0x0A: line_feed(); return;
		case 0x0C: form_feed(); return;
		case 0x08:
			st_.x_pos -= 1.0f / (float)st_.pitch_cpi;
			if (st_.x_pos < st_.left_margin_in)
				st_.x_pos = st_.left_margin_in;
			return;
		default:
			if (b >= 0x20)
				emit_char(b);
			return;
		}

	case State::Esc:
		if (b == 'E') {
			st_ = PrinterState{};
			apply_config(cfg_);
			state_ = State::Normal;
		} else if (b == '9') {
			state_ = State::Normal;
		} else if (b >= 0x21 && b <= 0x2F) {
			group_ = (char)b;
			state_ = State::TwoChar;
		} else {
			state_ = State::Normal;
		}
		return;

	case State::TwoChar:
		if (b >= 0x60 && b <= 0x7E) {
			sub_group_ = (char)b;
			param_pos_ = 0;
			param_buf_[0] = 0;
			state_ = State::Parameterized;
		} else {
			state_ = State::Normal;
		}
		return;

	case State::Parameterized:
		if ((b >= '0' && b <= '9') || b == '.' || b == '-' || b == '+') {
			if (param_pos_ < (int)sizeof(param_buf_) - 1)
				param_buf_[param_pos_++] = (char)b;
			return;
		}
		param_buf_[param_pos_] = 0;
		float value = (param_pos_ > 0) ? (float)atof(param_buf_) : 0.0f;

		if (b >= 'A' && b <= 'Z') {
			apply_param(group_, sub_group_, value, (char)b);
			state_ = State::Normal;
		} else if (b >= 'a' && b <= 'z') {
			apply_param(group_, sub_group_, value, (char)toupper(b));
			param_pos_ = 0;
			param_buf_[0] = 0;
		} else {
			state_ = State::Normal;
		}
		return;
	}
}

void PclPrinter::apply_param(char group, char sub_group, float value, char term)
{
	int ival = (int)value;

	if (group == '(' && sub_group == 's') {
		switch (term) {
		case 'P':
			if (ival == 0) { /* fixed pitch */ }
			break;
		case 'H':
			if (value >= 14.5f) st_.pitch_cpi = 15;
			else if (value >= 11.0f) st_.pitch_cpi = 12;
			else st_.pitch_cpi = 10;
			break;
		case 'B':
			st_.bold = (ival >= 3);
			break;
		default: break;
		}
	}
	else if (group == '&' && sub_group == 'd') {
		switch (term) {
		case 'D':
			st_.underline = true;
			break;
		case '@':
			st_.underline = false;
			break;
		default: break;
		}
	}
	else if (group == '&' && sub_group == 'l') {
		switch (term) {
		case 'D':
			if (ival == 1) st_.line_spacing_in = 1.0f / 6.0f;
			else if (ival == 2) st_.line_spacing_in = 1.0f / 3.0f;
			else if (ival == 3) st_.line_spacing_in = 0.5f;
			break;
		case 'E':
			st_.top_margin_in = (float)ival / 6.0f;
			break;
		case 'F':
			break;
		case 'A':
			if (ival == 2) {
				st_.page_width_in = 8.5f;
				st_.page_height_in = 11.0f;
			} else if (ival == 26) {
				st_.page_width_in = 8.27f;
				st_.page_height_in = 11.69f;
			}
			break;
		default: break;
		}
	}
	else if (group == '&' && sub_group == 'a') {
		switch (term) {
		case 'C':
			st_.x_pos = st_.left_margin_in + (float)ival / (float)st_.pitch_cpi;
			break;
		case 'R':
			st_.y_pos = st_.top_margin_in + (float)ival * st_.line_spacing_in;
			break;
		default: break;
		}
	}
	else if (group == '&' && sub_group == 'k') {
		if (term == 'H') {
			if (value >= 14.5f) st_.pitch_cpi = 15;
			else if (value >= 11.0f) st_.pitch_cpi = 12;
			else st_.pitch_cpi = 10;
		}
	}
}

std::unique_ptr<PrinterSim> create_pcl_printer(PrinterModel model, PdfWriter &pdf)
{
	return std::make_unique<PclPrinter>(model, pdf);
}
