PROJECT(MONOTONE)

############################################################################
# a few simple configure-y bits; stolen from configure.ac
############################################################################
IF (WIN32)
  SET(os_err_t unsigned int)
ELSE (WIN32)
  SET(os_err_t int)
ENDIF (WIN32)

INCLUDE(CheckIncludeFiles)
CHECK_INCLUDE_FILES(unistd.h HAVE_UNISTD_H)
CHECK_INCLUDE_FILES(errno.h HAVE_ERRNO_H)
CHECK_INCLUDE_FILES(alloca.h HAVE_ALLOCA_H)
CHECK_INCLUDE_FILES(float.h HAVE_FLOAT_H)
CHECK_INCLUDE_FILES(libintl.h HAVE_LIBINTL_H)
CHECK_INCLUDE_FILES(mcheck.h HAVE_MCHECK_H)

INCLUDE(CheckFunctionExists)
CHECK_FUNCTION_EXISTS(strerror HAVE_STRERROR)
CHECK_FUNCTION_EXISTS(strptime HAVE_STRPTIME)
CHECK_FUNCTION_EXISTS(mtrace HAVE_MTRACE)
CHECK_FUNCTION_EXISTS(getuid HAVE_GETUID)
CHECK_FUNCTION_EXISTS(geteuid HAVE_GETEUID)
# XXX: configure.ac checks first, then tries again with -lucb for this:
CHECK_FUNCTION_EXISTS(getreuid HAVE_GETEUID)

# XXX: how do we make a missing lib fatal here?
INCLUDE(CheckLibraryExists)
CHECK_LIBRARY_EXISTS(dl dlsym "" USE_DLOPEN)
CHECK_LIBRARY_EXISTS(resolv inet_aton "" USE_RESOLV)
CHECK_LIBRARY_EXISTS(socket accept "" USE_SOCKET)
CHECK_LIBRARY_EXISTS(ns inet_ntoa "" USE_NS)
CHECK_LIBRARY_EXISTS(rt fdatasync "" USE_FDATASYNC)
CHECK_LIBRARY_EXISTS(rt usleep "" USE_USLEEP)
CHECK_LIBRARY_EXISTS(z deflate "" USE_ZLIB)

INCLUDE(CheckTypeSize)
SET(CMAKE_EXTRA_INCLUDE_FILES sys/types.h)
CHECK_TYPE_SIZE("off_t" OFF_T)
CHECK_TYPE_SIZE("pid_t" PID_T)
CHECK_TYPE_SIZE("size_t" SIZE_T)
SET(CMAKE_EXTRA_INCLUDE_FILES)
SET(CMAKE_EXTRA_INCLUDE_FILES sys/types.h;sys/socket.h)
CHECK_TYPE_SIZE("socklen_t" SOCKLEN_T)
SET(CMAKE_EXTRA_INCLUDE_FILES)
IF(NOT HAVE_OFF_T)
  SET(off_t "long int")
ENDIF(NOT HAVE_OFF_T)
IF(NOT HAVE_PID_T)
  SET(pid_t int)
ENDIF(NOT HAVE_PID_T)
IF(NOT HAVE_SIZE_T)
  SET(size_t "unsigned int")
ENDIF(NOT HAVE_SIZE_T)
IF(NOT HAVE_SOCKLEN_T)
  SET(socklen_t int)
ENDIF(NOT HAVE_SOCKLEN_T)

# XXX: need to try doing fancier things like we have in configure.ac,
# including the ugly but fancy stuff like SYNC_WITH_STDIO_WORKS.

# this creates config.h from config.h.cmake.in (ala config.h.in in autoconf
# land).  for now we'll create it as config.h.cmake.out so as not to clobber
# our existing config.h.
# are we really expected to create config.h.cmake by hand, though? :-/
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake.in
               ${CMAKE_CURRENT_BINARY_DIR}/config.h)

############################################################################
# 
############################################################################
SET(CMAKE_C_FLAGS "-Wall")
SET(CMAKE_CXX_FLAGS "-Wall")

ADD_DEFINITIONS(-DHAVE_CONFIG_H)
ADD_DEFINITIONS(-DLOCALEDIR='"/usr/local/share/locale"')

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR} ${DIRS} /usr/include/boost)

SET(CMD_SOURCES
cmd.hh cmd_netsync.cc cmd_list.cc cmd_packet.cc cmd_key_cert.cc
cmd_merging.cc cmd_db.cc cmd_diff_log.cc cmd_ws_commit.cc
cmd_othervcs.cc cmd_automate.cc cmd_files.cc
)

SET(SANITY_CORE_SOURCES
sanity.cc
simplestring_xform.cc
constants.cc
numeric_vocab.cc
)

SET(LUAEXT_SOURCES
vocab.cc
charset.cc paths.cc
luaext_mkstemp.cc luaext_parse_basic_io.cc
luaext_guess_binary.cc luaext_platform.cc luaext_globish.cc
lua.cc mkstemp.cc file_io.cc
globish.cc basic_io.cc
)

SET(MOST_SOURCES
${SANITY_CORE_SOURCES} ${LUAEXT_SOURCES}
mtn-sanity.cc ui.cc
app_state.cc
commands.cc ${CMD_SOURCES}
diff_patch.cc
lua_hooks.cc
transforms.cc
update.cc
work.cc
cert.cc
database.cc
key_store.cc
localized_file_io.cc
keys.cc
packet.cc
rcs_file.cc
xdelta.cc
schema_migration.cc
refiner.cc
enumerator.cc
netsync.cc
netxx_pipe.cc
netcmd.cc
merkle_tree.cc
lcs.cc
rcs_import.cc
revision.cc
cset.cc
roster.cc
mt_version.cc
automate.cc
database_check.cc
epoch.cc
inodeprint.cc
selectors.cc
annotate.cc
restrictions.cc
hmac.cc
string_queue.cc
roster_merge.cc
merge.cc
legacy.cc
graph.cc
roster_delta.cc
sha1.cc
rev_height.cc

randomizer.cc
package_revision.c
package_full_revision.c
options.cc
)

SET(NETXX_SOURCES
	netxx/accept.cxx netxx/address.cxx
	netxx/datagram.cxx
	netxx/datagramserver.cxx netxx/osutil.cxx
	netxx/peer.cxx netxx/probe.cxx
	netxx/probe_select.cxx netxx/recvfrom.cxx
	netxx/serverbase.cxx
	netxx/sockaddr.cxx netxx/socket.cxx
	netxx/sockopt.cxx netxx/stream.cxx
	netxx/streambase.cxx netxx/streamserver.cxx
)

SET(BOTAN_SOURCES
        botan/aes.cpp botan/aes_tab.cpp
        botan/arc4.cpp botan/alg_id.cpp
        botan/asn1_alt.cpp botan/asn1_att.cpp botan/asn1_dn.cpp
        botan/asn1_ku.cpp botan/asn1_oid.cpp
        botan/asn1_str.cpp botan/asn1_tm.cpp
        botan/base.cpp botan/base64.cpp botan/basefilt.cpp
        botan/ber_dec.cpp botan/big_base.cpp
        botan/big_code.cpp botan/big_io.cpp botan/big_ops2.cpp
        botan/big_ops3.cpp botan/blinding.cpp botan/buf_es.cpp
        botan/buf_filt.cpp botan/cbc.cpp botan/certstor.cpp
        botan/cfb.cpp botan/charset.cpp botan/crc32.cpp
        botan/crl_ent.cpp botan/ctr.cpp botan/cts.cpp
        botan/data_snk.cpp botan/data_src.cpp
        botan/def_ops.cpp botan/defalloc.cpp
        botan/der_enc.cpp botan/divide.cpp botan/dl_algo.cpp
        botan/dl_group.cpp
        botan/dlies.cpp botan/eax.cpp botan/ecb.cpp botan/eme1.cpp
        botan/eme_pkcs.cpp botan/emsa1.cpp botan/emsa2.cpp
        botan/emsa3.cpp botan/emsa4.cpp botan/emsa_raw.cpp
        botan/eng_base.cpp botan/engine.cpp botan/es_file.cpp
        botan/exceptn.cpp botan/filter.cpp botan/filters.cpp
        botan/fips140.cpp
        botan/get_algo.cpp botan/get_enc.cpp botan/get_pbe.cpp
        botan/gzip.cpp botan/hash_id.cpp botan/hex.cpp botan/hmac.cpp
        botan/if_algo.cpp botan/inifile.cpp botan/init_def.cpp
        botan/kdf.cpp botan/keypair.cpp botan/look_pk.cpp
        botan/make_prm.cpp botan/mdx_hash.cpp
        botan/mem_pool.cpp botan/mgf1.cpp botan/mlock.cpp
        botan/mode_pad.cpp botan/modebase.cpp
        botan/mp_comba.cpp botan/mp_mulop.cpp
        botan/mp_misc.cpp botan/mp_mul.cpp botan/mp_shift.cpp
        botan/mutex.cpp botan/numthry.cpp
        botan/ofb.cpp botan/oids.cpp botan/parsing.cpp botan/pbes1.cpp
        botan/pbes2.cpp botan/pem.cpp botan/pipe.cpp botan/pipe_io.cpp
        botan/pipe_rw.cpp botan/pk_algs.cpp botan/pk_core.cpp
        botan/pk_filts.cpp botan/pk_keys.cpp botan/pk_util.cpp
        botan/pkcs10.cpp botan/pkcs8.cpp botan/policy.cpp
        botan/pow_mod.cpp
        botan/prf_x942.cpp botan/primes.cpp botan/pubkey.cpp
        botan/randpool.cpp botan/reducer.cpp botan/rng.cpp
        botan/rsa.cpp botan/s2k.cpp botan/secqueue.cpp
        botan/sha160.cpp botan/symkey.cpp botan/timers.cpp
        botan/ui.cpp botan/util.cpp botan/x509_ca.cpp
        botan/x509_crl.cpp botan/x509_key.cpp botan/x509_obj.cpp
        botan/x509cert.cpp botan/x509find.cpp botan/x509opt.cpp
        botan/des.cpp botan/des_tab.cpp botan/pkcs5.cpp
        botan/x509self.cpp botan/x509stor.cpp
        botan/def_alg.cpp botan/def_mode.cpp botan/sha256.cpp
        botan/x931_rng.cpp botan/out_buf.cpp botan/bit_ops.cpp
        botan/big_rand.cpp botan/def_powm.cpp botan/libstate.cpp
        botan/modules.cpp botan/mp_numth.cpp
        botan/mp_sqr.cpp botan/powm_fw.cpp
        botan/powm_mnt.cpp botan/mp_asm.cpp
	botan/asn1_int.cpp botan/config.cpp botan/datastor.cpp
	botan/def_char.cpp botan/init_opt.cpp botan/jacobi.cpp
	botan/x509_ext.cpp botan/x509stat.cpp
)

SET(BOOST_SANDBOX_SOURCES
	boost/circular_buffer_adaptor.hpp
        boost/circular_buffer_base.hpp
	boost/circular_buffer_fwd.hpp
        boost/circular_buffer.hpp
)

SET(SQLITE_SOURCES
	sqlite/alter.c sqlite/attach.c sqlite/auth.c sqlite/btree.c	
        sqlite/build.c sqlite/date.c sqlite/delete.c sqlite/expr.c	
        sqlite/func.c sqlite/hash.c sqlite/insert.c			
	sqlite/legacy.c	sqlite/main.c sqlite/opcodes.c			
	sqlite/os.c sqlite/os_unix.c sqlite/os_win.c sqlite/os_os2.c	
	sqlite/pager.c sqlite/parse.c sqlite/pragma.c sqlite/printf.c	
	sqlite/random.c sqlite/select.c					
	sqlite/table.c sqlite/tokenize.c				
	sqlite/trigger.c sqlite/update.c sqlite/utf.c			
	sqlite/util.c sqlite/vacuum.c					
	sqlite/vdbe.c sqlite/vdbeapi.c sqlite/vdbeaux.c			
	sqlite/vdbemem.c sqlite/where.c					
        sqlite/prepare.c sqlite/callback.c				
	sqlite/analyze.c sqlite/vdbefifo.c				
	sqlite/complete.c						
	sqlite/loadext.c sqlite/vtab.c					
)

SET(LUA_SOURCES
	lua/lapi.cc
	lua/lcode.cc
	lua/ldebug.cc
	lua/ldo.cc
	lua/ldump.cc							
	lua/lfunc.cc
	lua/lgc.cc
	lua/linit.cc							
	lua/llex.cc
	lua/lmem.cc
	lua/lobject.cc
	lua/lopcodes.cc
	lua/lparser.cc
	lua/lstate.cc
	lua/lstring.cc
	lua/ltable.cc
	lua/ltests.cc							
	lua/ltm.cc
	lua/lundump.cc
	lua/lvm.cc
	lua/lzio.cc
									
	lua/lauxlib.cc							
	lua/lbaselib.cc							
	lua/ldblib.cc							
	lua/liolib.cc							
	lua/lmathlib.cc							
	lua/loadlib.cc							
	lua/lstrlib.cc							
	lua/ltablib.cc							
	lua/loslib.cc
)

SET(IDNA_SOURCES
	idna/nfkc.c
	idna/punycode.c idna/rfc3454.c idna/version.c
	idna/idna.c idna/profiles.c
	idna/stringprep.c idna/toutf8.c
)

SET(UNIX_PLATFORM_SOURCES
	unix/read_password.cc unix/get_system_flavour.cc
	unix/process.cc unix/terminal.cc unix/inodeprint.cc
	unix/fs.cc unix/make_io_binary.cc unix/os_strerror.cc
	unix/cputime.cc
)

SET(WIN32_PLATFORM_SOURCES
	win32/read_password.cc win32/get_system_flavour.cc
	win32/process.cc win32/terminal.cc win32/inodeprint.cc
	win32/fs.cc win32/make_io_binary.cc win32/os_strerror.cc
	win32/cputime.cc
)

IF (WIN32)
  ADD_LIBRARY(libplatform STATIC ${WIN32_PLATFORM_SOURCES})
  SET(MTN_PLATFORM_SOURCES win32/main.cc)
  SET(MTN_PLATFORM_LIBRARIES shfolder ws2_32 intl iconv iphlpapi)
  SET(UNIT_TESTS_PLATFORM_LIBRARIES shfolder ws2_32 intl iconv iphlpapi)
  SET(LIB3RDPARTY_PLATFORM_SOURCES botan/es_capi.cpp botan/es_win32.cpp)
ELSE (WIN32)
  ADD_LIBRARY(libplatform STATIC ${UNIX_PLATFORM_SOURCES})
  SET(MTN_PLATFORM_SOURCES unix/main.cc)
  SET(MTN_PLATFORM_LIBRARIES)
  SET(UNIT_TESTS_PLATFORM_LIBRARIES)
  SET(LIB3RDPARTY_PLATFORM_SOURCES)
ENDIF (WIN32)

ADD_LIBRARY(lib3rdparty STATIC 
			${BOOST_SANDBOX_SOURCES}
			${BOTAN_SOURCES}
			${IDNA_SOURCES}
			${NETXX_SOURCES}
			${LUA_SOURCES}
			${SQLITE_SOURCES}
			${LIB3RDPARTY_PLATFORM_SOURCES})

ADD_EXECUTABLE(txt2c txt2c.cc)
GET_TARGET_PROPERTY(TXT2C_EXE txt2c LOCATION)

IF (WIN32)
  SET(MKPACKREV ./win32/mkpackrev.bat)
  SET(MKPACKFULLREV ./win32/mkpackfullrev.bat)
ELSE (WIN32)
  SET(MKPACKREV ./unix/mkpackrev.sh)
  SET(MKPACKFULLREV ./unix/mkpackfullrev.sh)
ENDIF (WIN32)
ADD_CUSTOM_COMMAND(
  OUTPUT package_revision.txt package_revision.c
  COMMAND ${MKPACKREV} ${CMAKE_CURRENT_SOURCE_DIR}
		       ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${MKPACKREV} txt2c ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
  )
ADD_CUSTOM_COMMAND(
  OUTPUT package_full_revision_dist.txt package_full_revision.txt
	 package_full_revision.c
  COMMAND ${MKPACKFULLREV} ${CMAKE_CURRENT_SOURCE_DIR}
			   ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${MKPACKFULLREV} txt2c
  )

ADD_EXECUTABLE(unit_tests ${MOST_SOURCES} unit_tests.cc crypto_tests.cc)
TARGET_LINK_LIBRARIES(unit_tests
		      3rdparty 
		      boost_filesystem boost_regex
		      boost_date_time boost_program_options
		      platform)
ADD_EXECUTABLE(tester ${SANITY_CORE_SOURCES} ${LUAEXT_SOURCES} tester.cc)
TARGET_LINK_LIBRARIES(tester
		      3rdparty 
		      boost_filesystem boost_regex
		      boost_date_time boost_program_options
		      platform)

ADD_EXECUTABLE(usher contrib/usher.cc)

ADD_EXECUTABLE(mtn ${MOST_SOURCES})
LINK_DIRECTORIES(${DIRS})
TARGET_LINK_LIBRARIES(mtn
		      3rdparty
		      boost_filesystem boost_regex
		      boost_date_time boost_program_options z
		      platform
		      ${MTN_PLATFORM_LIBRARIES})
