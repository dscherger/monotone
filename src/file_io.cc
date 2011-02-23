// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <iostream>
#include <fstream>

#include <botan/botan.h>
#include "botan_pipe_cache.hh"

#include "file_io.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "charset.hh"
#include "platform-wrapped.hh"
#include "numeric_vocab.hh"
#include "vocab_cast.hh"

// this file deals with talking to the filesystem, loading and
// saving files.

using std::cin;
using std::ifstream;
using std::ios_base;
using std::logic_error;
using std::string;
using std::vector;

void
assert_path_is_nonexistent(any_path const & path)
{
  I(get_path_status(path) == path::nonexistent);
}

void
assert_path_is_file(any_path const & path)
{
  I(get_path_status(path) == path::file);
}

void
assert_path_is_directory(any_path const & path)
{
  I(get_path_status(path) == path::directory);
}

void
require_path_is_nonexistent(any_path const & path,
                            i18n_format const & message)
{
  E(!path_exists(path), origin::user, message);
}

void
require_path_is_file(any_path const & path,
                     i18n_format const & message_if_nonexistent,
                     i18n_format const & message_if_directory)
{
  switch (get_path_status(path))
    {
    case path::nonexistent:
      E(false, origin::user, message_if_nonexistent);
      break;
    case path::file:
      return;
    case path::directory:
      E(false, origin::user, message_if_directory);
      break;
    }
}

void
require_path_is_directory(any_path const & path,
                          i18n_format const & message_if_nonexistent,
                          i18n_format const & message_if_file)
{
  switch (get_path_status(path))
    {
    case path::nonexistent:
      E(false, origin::user, message_if_nonexistent);
      break;
    case path::file:
      E(false, origin::user, message_if_file);
    case path::directory:
      return;
      break;
    }
}

bool
path_exists(any_path const & p)
{
  return get_path_status(p) != path::nonexistent;
}

bool
directory_exists(any_path const & p)
{
  return get_path_status(p) == path::directory;
}

bool
file_exists(any_path const & p)
{
  return get_path_status(p) == path::file;
}

bool
directory_empty(any_path const & path)
{
  struct directory_not_empty_exception {};
  struct directory_empty_helper : public dirent_consumer
  {
    virtual void consume(char const *)
    { throw directory_not_empty_exception(); }
  };

  directory_empty_helper h;
  try {
    read_directory(path, h, h, h);
  } catch (directory_not_empty_exception) {
    return false;
  }
  return true;
}

// This is not the greatest heuristic ever; it just looks for characters in
// the original ASCII control code range (00 - 1f, 7f) that are not white
// space (07 - 0D).  But then, GNU diff just looks for NULs.  We could do
// better if this was detecting character encoding (because then we could
// report wide encodings as such instead of treating them as binary) but the
// application proper isn't set up for that.
//
// Everything > 128 *can* be a valid text character, depending on encoding,
// even in the 80 - 9F region that Unicode reserves for yet more useless
// control characters.
//
// N.B. the obvious algorithmic version of the inner loop here
//     u8 c = s[i];
//     if (c <= 0x06 || (c >= 0x0E && c < 0x20) || c == 0x7F)
//       return true;
// is about twice as slow on current hardware (Intel Core2 quad).

bool guess_binary(string const & s)
{
  static const bool char_is_binary[256] = {
  //_0 _1 _2 _3 _4 _5 _6 _7 _8 _9 _A _B _C _D _E _F
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, // 0_
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 1_
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 2_
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 3_
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 4_
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 5_
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 6_
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, // 7_
    0                                               // 8+
  };

  for (size_t i = 0; i < s.size(); ++i)
    {
      if (char_is_binary[ static_cast<u8>(s[i]) ])
        return true;
    }
  return false;
}

void
mkdir_p(any_path const & p)
{
  switch (get_path_status(p))
    {
    case path::directory:
      return;
    case path::file:
      E(false, origin::system,
        F("could not create directory '%s': it is a file") % p);
    case path::nonexistent:
      std::string const current = p.as_external();
      any_path const parent = p.dirname();
      if (current != parent.as_external())
        {
          mkdir_p(parent);
        }
      do_mkdir(current);
    }
}

void
make_dir_for(any_path const & p)
{
  mkdir_p(p.dirname());
}

void
delete_file(any_path const & p)
{
  require_path_is_file(p,
                       F("file to delete '%s' does not exist") % p,
                       F("file to delete, '%s', is not a file but a directory") % p);
  do_remove(p.as_external());
}

void
delete_dir_shallow(any_path const & p)
{
  require_path_is_directory(p,
                            F("directory to delete '%s' does not exist") % p,
                            F("directory to delete, '%s', is not a directory but a file") % p);
  do_remove(p.as_external());
}

void
delete_file_or_dir_shallow(any_path const & p)
{
  E(path_exists(p), origin::user,
    F("object to delete, '%s', does not exist") % p);
  do_remove(p.as_external());
}

void
delete_dir_recursive(any_path const & p)
{
  require_path_is_directory(p,
                            F("directory to delete, '%s', does not exist") % p,
                            F("directory to delete, '%s', is a file") % p);

  do_remove_recursive(p.as_external());
}

void
move_file(any_path const & old_path,
          any_path const & new_path)
{
  require_path_is_file(old_path,
                       F("rename source file '%s' does not exist") % old_path,
                       F("rename source file '%s' is a directory "
                         "-- bug in monotone?") % old_path);
  require_path_is_nonexistent(new_path,
                              F("rename target '%s' already exists")
                              % new_path);
  rename_clobberingly(old_path.as_external(), new_path.as_external());
}

void
move_dir(any_path const & old_path,
         any_path const & new_path)
{
  require_path_is_directory(old_path,
                            F("rename source dir '%s' does not exist")
                            % old_path,
                            F("rename source dir '%s' is a file "
                              "-- bug in monotone?") % old_path);
  require_path_is_nonexistent(new_path,
                              F("rename target '%s' already exists")
                              % new_path);
  rename_clobberingly(old_path.as_external(), new_path.as_external());
}

void
move_path(any_path const & old_path,
          any_path const & new_path)
{
  E(path_exists(old_path), origin::user,
    F("rename source path '%s' does not exist") % old_path);
  require_path_is_nonexistent(new_path,
                              F("rename target '%s' already exists")
                              % new_path);
  rename_clobberingly(old_path.as_external(), new_path.as_external());
}

void
read_data(any_path const & p, data & dat)
{
  require_path_is_file(p,
                       F("file '%s' does not exist") % p,
                       F("file '%s' cannot be read as data; it is a directory") % p);

  ifstream file(p.as_external().c_str(),
                ios_base::in | ios_base::binary);
  E(file, origin::user, F("cannot open file '%s' for reading") % p);
  unfiltered_pipe->start_msg();
  file >> *unfiltered_pipe;
  unfiltered_pipe->end_msg();
  origin::type data_from = p.made_from;
  if (data_from != origin::internal || data_from == origin::database)
    data_from = origin::system;
  dat = data(unfiltered_pipe->read_all_as_string(Botan::Pipe::LAST_MESSAGE),
             data_from);
}

// This function can only be called once per run.
void
read_data_stdin(data & dat)
{
  static bool have_consumed_stdin = false;
  E(!have_consumed_stdin, origin::user,
    F("Cannot read standard input multiple times"));
  have_consumed_stdin = true;
  unfiltered_pipe->start_msg();
  cin >> *unfiltered_pipe;
  unfiltered_pipe->end_msg();
  dat = data(unfiltered_pipe->read_all_as_string(Botan::Pipe::LAST_MESSAGE),
             origin::user);
}

void
read_data_for_command_line(utf8 const & path, data & dat)
{
  if (path() == "-")
    read_data_stdin(dat);
  else
    read_data(system_path(path), dat);
}


// FIXME: this is probably not enough brains to actually manage "atomic
// filesystem writes". at some point you have to draw the line with even
// trying, and I'm not sure it's really a strict requirement of this tool,
// but you might want to make this code a bit tighter.

static void
write_data_impl(any_path const & p,
                data const & dat,
                any_path const & tmp,
                bool user_private)
{
  E(!directory_exists(p), origin::user,
    F("file '%s' cannot be overwritten as data; it is a directory") % p);

  make_dir_for(p);

  write_data_worker(p.as_external(), dat(), tmp.as_external(), user_private);
}

void
write_data(file_path const & path, data const & dat)
{
  // use the bookkeeping root as the temporary directory.
  assert_path_is_directory(bookkeeping_root);
  write_data_impl(path, dat, bookkeeping_root, false);
}

void
write_data(bookkeeping_path const & path, data const & dat)
{
  // use the bookkeeping root as the temporary directory.
  assert_path_is_directory(bookkeeping_root);
  write_data_impl(path, dat, bookkeeping_root, false);
}

void
write_data(system_path const & path,
           data const & data,
           system_path const & tmpdir)
{
  write_data_impl(path, data, tmpdir, false);
}

void
write_data_userprivate(system_path const & path,
                       data const & data,
                       system_path const & tmpdir)
{
  write_data_impl(path, data, tmpdir, true);
}

// recursive directory walking

tree_walker::~tree_walker() {}

bool
tree_walker::visit_dir(file_path const & path)
{
  return true;
}

static void
walk_tree_recursive(file_path const & path,
                    tree_walker & walker)
{
  // Read the directory up front, so that the directory handle is released
  // before we recurse.  This is important, because it can allocate rather a
  // bit of memory (especially on ReiserFS, see [1]; opendir uses the
  // filesystem's blocksize as a clue how much memory to allocate).  We used
  // to recurse into subdirectories on the fly; this left the memory
  // describing _this_ directory pinned on the heap.  Then our recursive
  // call itself made another recursive call, etc., causing a huge spike in
  // peak memory.  By splitting the loop in half, we avoid this problem.
  //
  // [1] http://lkml.org/lkml/2006/2/24/215
  vector<file_path> files, dirs;
  fill_path_vec<file_path> fill_files(path, files, false);
  fill_path_vec<file_path> fill_dirs(path, dirs, true);

  read_directory(path, fill_files, fill_dirs);

  for (vector<file_path>::const_iterator i = files.begin();
       i != files.end(); ++i)
    walker.visit_file(*i);

  for (vector<file_path>::const_iterator i = dirs.begin();
       i != dirs.end(); ++i)
    if (walker.visit_dir(*i))
      walk_tree_recursive(*i, walker);
}

// from some (safe) sub-entry of cwd
void
walk_tree(file_path const & path, tree_walker & walker)
{
  if (path.empty())
    {
      walk_tree_recursive(path, walker);
      return;
    }

  switch (get_path_status(path))
    {
    case path::nonexistent:
      E(false, origin::user, F("no such file or directory: '%s'") % path);
      break;
    case path::file:
      walker.visit_file(path);
      break;
    case path::directory:
      if (walker.visit_dir(path))
        walk_tree_recursive(path, walker);
      break;
    }
}

bool
ident_existing_file(file_path const & p, file_id & ident)
{
  return ident_existing_file(p, ident, get_path_status(p));
}

bool
ident_existing_file(file_path const & p, file_id & ident, path::status status)
{
  switch (status)
    {
    case path::nonexistent:
      return false;
    case path::file:
      break;
    case path::directory:
      W(F("expected file '%s', but it is a directory.") % p);
      return false;
    }

  calculate_ident(p, ident);
  return true;
}

void
calculate_ident(file_path const & file,
                file_id & ident)
{
  // no conversions necessary, use streaming form
  static cached_botan_pipe
    p(new Botan::Pipe(new Botan::Hash_Filter("SHA-160")));

  // Best to be safe and check it isn't a dir.
  assert_path_is_file(file);
  Botan::DataSource_Stream infile(file.as_external(), true);
  p->process_msg(infile);

  ident = file_id(p->read_all_as_string(Botan::Pipe::LAST_MESSAGE),
                  origin::internal);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
