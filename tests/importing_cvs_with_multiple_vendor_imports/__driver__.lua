include("common/cvs.lua")
mtn_setup()

writefile("fileA.0", "fileA from VendorA")
writefile("fileA.1", "fileA from VendorA - changed")
writefile("fileB.0", "fileB from VendorB")
writefile("fileB.1", "fileB from VendorB - changed")
writefile("fileC.0", "our own additions") 

cvs_setup()

-- the first vendor import
mkdir("vendorA_dir")
copy("fileA.0", "vendorA_dir/fileA")
check(indir("vendorA_dir", cvs("import", "-m", "Initial import from VendorA", "testsrc", "VendorA", "VendorA_REL_1")), 0, false, false)

-- the second vendor import
mkdir("vendorB_dir")
copy("fileB.0", "vendorB_dir/fileB")
check(indir("vendorB_dir", cvs("import", "-m", "Initial import from VendorB", "testsrc", "VendorB", "VendorB_REL_1")), 0, false, false)

-- checkout the repository and commit some files
check(cvs("co", "testsrc"), 0, false, false)
copy("fileC.0", "testsrc/fileC")
check(indir("testsrc", cvs("add", "fileC")), 0, false, false)
check(indir("testsrc", cvs("commit", "-m", 'commit 0')), 0, false, false)

-- updates from VendorA
copy("fileA.1", "vendorA_dir/fileA")
check(indir("vendorA_dir", cvs("import", "-m", "Initial import from VendorA", "testsrc", "VendorA", "VendorA_REL_2")), 0, false, false)

-- updates from VendorB
copy("fileB.1", "vendorB_dir/fileB")
check(indir("vendorB_dir", cvs("import", "-m", "Initial import from VendorA", "-b", "1.1.3", "testsrc", "VendorB", "VendorB_REL_2")), 0, false, false)

-- import into monotone and check presence of files
check(mtn("--branch=test", "--debug", "cvs_import", cvsroot.."/testsrc"), 0, false, false)

-- check if all non-empty branches were imported
check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.VendorA", "test.VendorB"}))

check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(samefile("fileA.0", "maindir/fileA"))
check(samefile("fileB.0", "maindir/fileB"))
check(samefile("fileC.0", "maindir/fileC"))

check(mtn("checkout", "--branch=test.VendorA", "vendorA_co"), 0, false, false)
check(samefile("fileA.1", "vendorA_co/fileA"))

check(mtn("checkout", "--branch=test.VendorB", "vendorB_co"), 0, false, false)
check(samefile("fileB.1", "vendorB_co/fileB"))
