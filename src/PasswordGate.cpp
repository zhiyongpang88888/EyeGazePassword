#include "PasswordGate.h"

void PasswordGate::inputDigit(char d)
{
    if (state_ != 0) return; // 已有结果，先 clear()
    if (buf_.size() < (size_t)kLen) buf_.push_back(d);
    if (buf_.size() == (size_t)kLen)
        state_ = verify() ? 1 : 2;
}

void PasswordGate::clear()
{
    buf_.clear();
    state_ = 0;
}

bool PasswordGate::verify() const
{
    return buf_ == password_;
}
