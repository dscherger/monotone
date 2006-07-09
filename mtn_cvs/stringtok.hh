// copyright (C) 2005 Christof Petig <christof@petig-baender.de>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// inspired by code from Marcelo E. Magallon and the libstdc++ doku

#include <string>

template <typename Container>
void
stringtok (Container &container, std::string const &in,
           const char * const delimiters = " \t\n")
{
    const std::string::size_type len = in.length();
          std::string::size_type i = 0;

    while ( i < len )
    {
        // find the end of the token
        std::string::size_type j = in.find_first_of (delimiters, i);

        // push token
        if (j == std::string::npos) {
            container.push_back (in.substr(i));
            return;
        } else
            container.push_back (in.substr(i, j-i));

        // set up for next loop
        i = j + 1;
    }
}

// an adaptor giving push_back on insert providing containers (sets)
template <typename Container>
 class push_back2insert
{ Container &c;
public:
  push_back2insert(Container &_c) : c(_c) {}
  template <typename T>
   void push_back(const T &t) const { c.insert(t); }
};

