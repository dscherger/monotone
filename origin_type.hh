#ifndef __ORIGIN_TYPE_HH__
#define __ORIGIN_TYPE_HH__

// sanity.cc:type_to_string(type t) will need to match this
namespace origin {
  enum type {
    internal,
    network,
    database,
    workspace,
    system,
    user,
    no_fault
  };
}

// Local Variables:
// mode: C++
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
