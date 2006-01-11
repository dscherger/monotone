#!/usr/bin/env python

import sys
sys.path.append("..")
import time
import monotone
import config
import traceback

def test_branches(mt):
    """Test the mt.branches method"""
    result = mt.branches()
    if len(result) == 0:
	raise Exception("No branches found")

def test_tags(mt):
    """Test the mt.tags method"""
    result = mt.tags()
    if len(result) == 0:
	raise Exception("No tags found")

def test_heads(mt):
    """Test the mt.heads method"""
    result = mt.heads(config.test_branch)
    if len(result) == 0:
	raise Exception("No heads found")

def test_certs(mt):
    """Test the mt.certs method"""
    result = mt.certs(config.test_revision)
    print result
    if len(result) == 0:
	raise Exception("No certs")

def test_ancestors(mt):
    """Test the mt.ancestors method"""
    result = mt.ancestors(config.test_revision)
    if len(result) == 0:
	raise Exception("No certs")

def test_toposort(mt):
    """Test the mt.toposort method"""
    tosort = mt.heads(config.test_branch)
    result = mt.toposort(tosort)
    if len(result) == 0:
	raise Exception("No result to toposort")

def test_revision(mt):
    """Test the mt.revision method"""
    rev = mt.revision(config.test_revision)
    if rev == None:
	raise Exception("Unable to retrieve revision")

def test_manifest(mt):
    """Test the mt.manifest method"""
    rev = mt.revision(config.test_revision)
    print rev
    return 
    if not rev.has_key('new_manifest'):
	raise Exception("Couldn't find manifest.")
    print rev
    return
    manifest = rev['new_manifest'][0][1]
    print manifest

def test_file(mt):
    pass

def test_crazy_toposort(mt):
    """Test huge (silly) arguments"""
    #ancestors = mt.heads(config.test_branch)
    ancestors = mt.ancestors(mt.heads(config.test_branch))
    while len(ancestors) < 1000:
	result = mt.toposort(ancestors)
	open('command.txt', 'w').write(config.monotone + 'automate ancestors ' + ' '.join(ancestors))
	ancestors *= 2

def test_error_string(mt):
    """Test what happens when we do something wrong resulting in an error"""
    try:
	heads = mt.certs('0000000000000000000000000000000000000000')
	raise Exception("No error occurred.")
    except Exception: pass

def test_invalid_data(mt):
    """Test what happens when we do something which is invalid"""
    try:
	mt.certs('0000') # too short
	raise Exception("No error occurred.")
    except Exception: pass

tests = [

# disabled as they don't use automate
#    test_branches,
#    test_tags,

    test_ancestors,
    test_toposort,
    test_revision,
    test_manifest,
    test_file,

    test_heads,
    test_certs,

    test_crazy_toposort,
    test_error_string,
    test_invalid_data,
]

def test():
    for idx, test in enumerate(tests):
	def log(s):
	    print "#%-3d %s" % (idx, s)
	log(test.__doc__)
	try:
	    test(mt)
	except KeyboardInterrupt:
	    break
	except:
	    log ("Test FAILED; traceback follows")
	    traceback.print_exc()
	print

if __name__ == "__main__":
    mt = monotone.Monotone(config.monotone, config.dbfile)
    if len(sys.argv) == 2:
	attempts = int(sys.argv[1])
    else:
	attempts = 1

    while attempts > 0:
	test()
	time.sleep(10)
	attempts -= 1
    mt.automate.stop()
