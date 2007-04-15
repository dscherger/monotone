-- CVS push with fork and merge

include("/common/cvs.lua")
mtn_setup()
cvs_setup()

function countMatch(data, match)
    count = 0
    for word in string.gmatch(data, match) do
        count = count + 1
    end
    return count
end

-- create a test project

mkdir("cvstemp")
writefile("cvstemp/A","A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\n")
check(indir("cvstemp", cvs("import","-m","initial import", "test", "vendor_tag", "initial_import")), 0, false, false)

-- change the repository in various ways
check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)

check(mtn("automate", "heads"), 0, true, false)
canonicalize("stdout")
heads = readfile("stdout")
base = string.sub(heads,0,40)

check(cvs("co", "test"), 0, false, false)
writefile("test/A","A\n-\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK\n")
check(indir("test", cvs("ci", "-m", "messageA")), 0, false, false)

check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)

check(mtn("automate", "heads"), 0, true, false)
canonicalize("stdout")
heads = readfile("stdout")
rev1 = string.sub(heads,0,40)

writefile("test/A","A\n-\nB\n-\nC\nD\nE\nF\nG\nH\nI\nJ\nK\n")
check(indir("test", cvs("ci", "-m", "messageB")), 0, false, false)

check(mtn_cvs("--branch=testbranch","pull",cvsroot,"test"),0, false,false)

check(mtn("automate", "heads"), 0, true, false)
canonicalize("stdout")
heads = readfile("stdout")
rev2 = string.sub(heads,0,40)

check(mtn("co","--branch=testbranch","test-mtn"),0, false, false)

writefile("test-mtn/A","A\n-\nB\n-\nC\n-\nD\nE\nF\nG\nH\nI\nJ\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageC")), 0, false, false)
writefile("test-mtn/A","A\n-\nB\n-\nC\n-\nD\n-\nE\nF\nG\nH\nI\nJ\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageD")), 0, false, false)

check(mtn("automate", "heads"), 0, true, false)
canonicalize("stdout")
heads = readfile("stdout")
rev3 = string.sub(heads,0,40)

check(indir("test-mtn",mtn_cvs("--branch=testbranch","push","--first")),0, false,false)

check(indir("test-mtn", mtn("up", "-r", base)), 0, false, false)

writefile("test-mtn/A","A\n\nB\nC\nD\nE\n-\nF\nG\nH\nI\nJ\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageE")), 0, false, false)
writefile("test-mtn/A","A\n\nB\nC\nD\nE\n-\nF\n-\nG\nH\nI\nJ\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageF")), 0, false, false)

check(indir("test-mtn", mtn("up", "-r", rev1)), 0, false, false)

writefile("test-mtn/A","A\nB\nC\nD\nE\nF\nG\n-\nH\nI\nJ\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageG")), 0, false, false)
writefile("test-mtn/A","A\nB\nC\nD\nE\nF\nG\n-\nH\n-\nI\nJ\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageH")), 0, false, false)

check(mtn("merge"), 0, false, false)

check(indir("test-mtn", mtn("up")), 0, false, false)

writefile("test-mtn/A","A\n-\nB\n-\nC\n-\nD\n-\nE\n-\nF\n-\nG\n-\nH\n-\nI\n-\nJ\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageI")), 0, false, false)
writefile("test-mtn/A","A\n-\nB\n-\nC\n-\nD\n-\nE\n-\nF\n-\nG\n-\nH\n-\nI\n-\nJ\n-\nK\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageJ")), 0, false, false)

check(indir("test-mtn", mtn("up", "-r", rev3)), 0, false, false)

writefile("test-mtn/A","A\n-\nB\n-\nC\n-\nD\n-\nE\nF\nG\nH\nI\nJ\nK\n-\nL\nM\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageK")), 0, false, false)
writefile("test-mtn/A","A\n-\nB\n-\nC\n-\nD\n-\nE\nF\nG\nH\nI\nJ\nK\n-\nL\n-\nM\n")
check(indir("test-mtn", mtn("ci", "-m", "mtn-messageL")), 0, false, false)

check(mtn("merge"), 0, false, false)

check(indir("test-mtn", mtn_cvs("--branch=testbranch","push","--first")),0, false,false)

check(indir("test", cvs("up")), 0, false, false)

check(indir("test", cvs("log", "A")), 0, true, false)
canonicalize("stdout")
log = readfile("stdout")

check(countMatch(log, "messageA") == 1, true, false, false)
check(countMatch(log, "messageB") == 1, true, false, false)
check(countMatch(log, "messageC") == 1, true, false, false)
check(countMatch(log, "messageD") == 1, true, false, false)
check(countMatch(log, "messageE") == 1, true, false, false)
check(countMatch(log, "messageF") == 1, true, false, false)
check(countMatch(log, "messageG") == 1, true, false, false)
check(countMatch(log, "messageH") == 1, true, false, false)
check(countMatch(log, "messageI") == 1, true, false, false)
check(countMatch(log, "messageJ") == 1, true, false, false)
check(countMatch(log, "messageK") == 1, true, false, false)
check(countMatch(log, "messageL") == 1, true, false, false)

