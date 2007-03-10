
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))


-- import into monotone and check presence of files
check(mtn("--branch=test", "--debug", "cvs_import", "cvs-repository/test"), 0, false, false)

-- check if all non-empty branches were imported
check(mtn("list", "branches"), 0, true, false)
check(samelines("stdout", {"test", "test.VendorA", "test.VendorB"}))

check(mtn("checkout", "--branch=test", "maindir"), 0, false, false)
check(samelines("maindir/fileA", {"fileA from VendorA"}))
check(samelines("maindir/fileB", {"fileB from VendorB"}))
check(samelines("maindir/fileC", {"our own additions"}))

check(mtn("checkout", "--branch=test.VendorA", "vendorA_co"), 0, false, false)
check(samelines("vendorA_co/fileA", {"fileA from VendorA - changed"}))

check(mtn("checkout", "--branch=test.VendorB", "vendorB_co"), 0, false, false)
check(samelines("vendorB_co/fileB", {"fileB from VendorB - changed"}))
