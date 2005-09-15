import random
import merkle_dir
import fs
import tempfile
import shutil

def flip():
    return random.randrange(2)

def randid():
    return "".join([random.choice("0123456789abcdef") for i in xrange(40)])

def randdata():
    length = random.choice([0, 1, None, None, None, None])
    if length is None:
        length = random.randrange(100)
    return "".join([chr(random.randrange(256)) for i in xrange(length)])

def add_to(md, expected):
    md.begin()
    num = random.randrange(10)
    for i in xrange(num):
        newid = randid()
        newdata = randdata()
        expected[newid] = newdata
        md.add(newid, newdata)
    md.commit()
    print "added %s items to a set" % num

def check_matches(md, expected):
    checked = 0
    for id, data in md.all_chunks():
        assert expected[id] == data
        checked += 1
    assert checked == len(expected)

def new_in(thing, versus):
    new = {}
    for key, val in thing.iteritems():
        if not versus.has_key(key):
            new[key] = val
    return new

class expecting_callback:
    def __init__(self, in_source, in_target):
        self.expected = new_in(in_source, in_target)
    def __call__(self, id, data):
        assert self.expected[id] == data
        del self.expected[id]
    def check_done(self):
        assert len(self.expected) == 0

def run_tests():
    random.seed(0)

    try:
        try:
            a_dir = tempfile.mkdtemp()
            b_dir = tempfile.mkdtemp()
            a_fs = fs.LocalWriteableFs(a_dir)
            b_fs = fs.LocalWriteableFs(b_dir)

            in_a = {}
            in_b = {}

            for i in xrange(1000):
                print i
                a = merkle_dir.MerkleDir(a_fs)
                b = merkle_dir.MerkleDir(b_fs)
                if flip():
                    add_to(a, in_a)
                if flip():
                    add_to(b, in_b)

                if flip():
                    subject_name = "a"
                    subject = a
                    in_subject = in_a
                    object_name = "b"
                    object = b
                    in_object = in_b
                else:
                    subject_name = "b"
                    subject = b
                    in_subject = in_b
                    object_name = "a"
                    object = a
                    in_object = in_a

                verb = random.choice(["push", "pull", "sync", "nothing"])
                print "%s(%s, %s)" % (verb, subject_name, object_name)
                if verb == "push":
                    new_in_subj_callback = expecting_callback(in_subject,
                                                              in_object)
                    subject.push(object, new_in_subj_callback)
                    new_in_subj_callback.check_done()
                    in_object.update(in_subject)
                elif verb == "pull":
                    new_in_obj_callback = expecting_callback(in_object,
                                                             in_subject)
                    subject.pull(object, new_in_obj_callback)
                    new_in_obj_callback.check_done()
                    in_subject.update(in_object)
                elif verb == "sync":
                    new_in_subj_callback = expecting_callback(in_subject,
                                                              in_object)
                    new_in_obj_callback = expecting_callback(in_object,
                                                             in_subject)
                    subject.sync(object,
                                 new_in_obj_callback, new_in_subj_callback)
                    new_in_subj_callback.check_done()
                    new_in_obj_callback.check_done()
                    in_subject.update(in_object)
                    in_object.update(in_subject)
                elif verb == "nothing":
                    pass

                check_matches(a, in_a)
                check_matches(b, in_b)
        except:
            import sys, pdb
            pdb.post_mortem(sys.exc_traceback)
    finally:
        shutil.rmtree(a_dir, ignore_errors=1)
        shutil.rmtree(b_dir, ignore_errors=1)


if __name__ == "__main__":
    run_tests()
