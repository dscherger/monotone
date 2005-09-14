import random
import merkle_dir
import fs
import tempfile
import shutil

def flip():
    return random.randrange(2)

def randid():
    return "".join([random.choice("0123456789zbcdef") for i in xrange(40)])

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

def run_tests():
    random.seed(0)

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
                subject = a
                in_subject = in_a
                object = b
                in_object = in_b
            else:
                subject = b
                in_subject = in_b
                object = a
                in_object = in_a
                
            verb = random.choice(["push", "pull", "sync"])
            print verb
            if verb == "push":
                subject.push(object)
                in_object.update(in_subject)
            elif verb == "pull":
                subject.pull(object)
                in_subject.update(in_object)
            elif verb == "sync":
                subject.sync(object)
                in_subject.update(in_object)
                in_object.update(in_subject)

            check_matches(a, in_a)
            check_matches(b, in_b)

    finally:
        #shutil.rmtree(a_dir, ignore_errors=1)
        #shutil.rmtree(b_dir, ignore_errors=1)
        pass


if __name__ == "__main__":
    run_tests()
