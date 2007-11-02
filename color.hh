#include <ostream>
#include <string>

class color {

private:
  const std::string code;
  color(const char * str);

public:
  static const color std, strong, blue, green, yellow, red, purple, cyan, gray;
  static const color diff_add;
  static const color diff_del;
  static const color diff_conflict;
  static const color comment;

  std::string toString() const;
  friend std::ostream & operator <<(std::ostream & os, const color & col);
};
