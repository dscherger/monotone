// this is how you "ask for" the C99 constant constructor macros.  *and*
// you have to do so before any other files accidentally include
// stdint.h. awesome.  (from change_set.cc, required for UINT32_C)
#define __STDC_CONSTANT_MACROS

#ifdef WIN32
#define BOOST_NO_STDC_NAMESPACE
#endif

#include <boost/bind.hpp>
#include <boost/config.hpp>
#include <boost/cstdlib.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/exception.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
// We have a local version of this to work around a bug in the MSVC debug checking.
#include "boost/format.hpp"
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/random.hpp>
#include <boost/regex.hpp>
#include <boost/scoped_array.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/static_assert.hpp>
#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test_suite.hpp>
#include <boost/tokenizer.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/version.hpp>
