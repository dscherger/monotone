#ifndef __AUTOMATE_OSTREAM_HH__
#define __AUTOMATE_OSTREAM_HH__

#include <iostream>

template<typename _CharT, typename _Traits = std::char_traits<_CharT> >
class basic_automate_streambuf : public std::basic_streambuf<_CharT, _Traits>
{
  typedef _Traits traits_type;
  typedef typename _Traits::int_type int_type;
  size_t _bufsize;
  std::basic_ostream<_CharT, _Traits> *out;
  int cmdnum;
  int err;
public:
  /*
  automate_streambuf(size_t bufsize)
    : std::streambuf(), _bufsize(bufsize), out(0), cmdnum(0), err(0)
  {
    char *inbuf = new char[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }
  */
  basic_automate_streambuf(std::ostream & o, size_t bufsize)
    : std::streambuf(), _bufsize(bufsize), out(&o), cmdnum(0), err(0)
  {
    _CharT *inbuf = new _CharT[_bufsize];
    setp(inbuf, inbuf + _bufsize);
  }
  ~basic_automate_streambuf()
  {}

  void set_err(int e)
  {
    sync();
    err = e;
  }

  void end_cmd()
  {
    _M_sync(true);
    ++cmdnum;
    err = 0;
  }

  virtual int sync()
  {
    _M_sync();
    return 0;
  }

  void _M_sync(bool end = false)
  {
    if (!out)
      {
        setp(this->pbase(), this->pbase() + _bufsize);
        return;
      }
    int num = this->pptr() - this->pbase();
    if (num || end)
      {
        (*out) << cmdnum << ':'
               << err << ':'
               << (end?'l':'m') << ':'
               << num << ':'
               << std::basic_string<_CharT,_Traits>(this->pbase(), num);
        setp(this->pbase(), this->pbase() + _bufsize);
        out->flush();
      }
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
    : std::ostream(NULL),
      _M_autobuf(out, blocksize)
  { this->init(&_M_autobuf); }

  ~basic_automate_ostream()
  {}

  streambuf_type *
  rdbuf() const
  { return const_cast<streambuf_type *>(&_M_autobuf); }

  void set_err(int e)
  { _M_autobuf.set_err(e); }

  void end_cmd()
  { _M_autobuf.end_cmd(); }
};

typedef basic_automate_streambuf<char> automate_streambuf;
typedef basic_automate_ostream<char> automate_ostream;

#endif
