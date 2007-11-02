#include "color.hh"

color::color(const char * str): code(str) { }
//{
//  code = std::string("\033[") + std::string(str) + std::string("m");
//}

const color color::std   ("0");
const color color::strong("1");
const color color::blue  ("31");
const color color::green ("32");
const color color::yellow("33");
const color color::red   ("34");
const color color::purple("35");
const color color::cyan  ("36");
const color color::gray  ("37");

const color color::diff_add = blue;
const color color::diff_del = red;
const color color::diff_conflict = purple;
const color color::comment = gray;

std::string
color::toString() const
{
  return std::string("\033[") + code + std::string("m");
}

std::ostream&
operator <<(std::ostream & os, const color & col)
{
  return os << col.toString();
}
