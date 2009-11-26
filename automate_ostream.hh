// Copyright (C) 2005 and later by various people
// see monotone commit logs for details and authors
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#ifndef __AUTOMATE_OSTREAM_HH__
#define __AUTOMATE_OSTREAM_HH__

#include <iostream>
#include "lexical_cast.hh"

using boost::lexical_cast;

template<typename _CharT, typename _Traits = std::char_traits<_CharT> >
class basic_automate_streambuf : public std::basic_streambuf<_CharT, _Traits>
{
  typedef _Traits traits_type;
  typedef typename _Traits::int_type int_type;
  size_t _bufsize;
  std::basic_ostream<_CharT, _Traits> *out;
  int cmdnum;

public:
  basic_automate_streambuf(std::ostream & o, size_t bufsize)
    : std::streambuf(), _bufsize(bufsize), out(&o), cmdnum(0)
  {
    _CharT *inbuf = new _CharT[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }

  basic_automate_streambuf()
  {}

  ~basic_automate_streambuf()
  {}

  void end_cmd(int errcode)
  {
    _M_sync();
    write_out_of_band('l', lexical_cast<std::string>(errcode));
    ++cmdnum;
  }

  virtual int sync()
  {
    _M_sync();
    return 0;
  }

  void _M_sync()
  {
    if (!out)
      {
        setp(this->pbase(), this->pbase() + _bufsize);
        return;
      }
    int num = this->pptr() - this->pbase();
    if (num)
      {
        (*out) << cmdnum << ':'
               << 'm' << ':'
               << num << ':'
               << std::basic_string<_CharT,_Traits>(this->pbase(), num);
        setp(this->pbase(), this->pbase() + _bufsize);
        out->flush();
      }
  }

  void write_out_of_band(char type, std::string const & data)
  {
    unsigned chunksize = _bufsize;
    size_t length = data.size(), offset = 0;
    do
    {
      if (offset+chunksize>length)
        chunksize = length-offset;
      (*out) << cmdnum << ':' << type << ':' << chunksize
             << ':' << data.substr(offset, chunksize);
      offset+= chunksize;
    } while (offset<length);
    out->flush();
  }

  int_type
  overflow(int_type c = traits_type::eof())
  {
    sync();
    sputc(c);
    return 0;
  }
};

template<typename _CharT, typename _Traits = std::char_traits<_CharT> >
struct basic_automate_ostream : public std::basic_ostream<_CharT, _Traits>
{
  typedef basic_automate_streambuf<_CharT, _Traits> streambuf_type;
  streambuf_type _M_autobuf;

  basic_automate_ostream(std::basic_ostream<_CharT, _Traits> &out,
                   size_t blocksize)
    : std::basic_ostream<_CharT, _Traits>(NULL),
      _M_autobuf(out, blocksize)
  { this->init(&_M_autobuf); }

protected:
  basic_automate_ostream() { }
public:

  virtual ~basic_automate_ostream()
  {}

  streambuf_type *
  rdbuf() const
  { return const_cast<streambuf_type *>(&_M_autobuf); }

  virtual void end_cmd(int errcode)
  { _M_autobuf.end_cmd(errcode); }

  virtual void write_out_of_band(char type, std::string const & data)
  { _M_autobuf.write_out_of_band(type, data); }
};

typedef basic_automate_streambuf<char> automate_streambuf;
typedef basic_automate_ostream<char> automate_ostream;

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
