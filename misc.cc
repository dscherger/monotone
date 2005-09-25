#include "misc.hh"
#include <fstream>
#include <iostream>

std::string readfile(std::string const & f)
{
  std::string out;
  std::ifstream is(f.c_str(), std::ios::binary);
  is.seekg(0, std::ios::end);
  int length = is.tellg();
  is.seekg(0, std::ios::beg);
  if (length != -1)
    {
      char *buffer = new char[length];
      is.read(buffer, length);
      out = std::string(buffer, length);
      delete[] buffer;
    }
  return out;
}
