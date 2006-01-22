/*************************************************
* Default Policy Source File                     *
* (C) 1999-2006 The Botan Project                *
*************************************************/

#include <botan/libstate.h>

namespace Botan {

namespace {

/*************************************************
* OID loading helper function                    *
*************************************************/
void add_oid(Library_State* state,
             const std::string& oid_str,
             const std::string& name)
   {
   if(!state->option_set("oid2str", oid_str))
      state->set_option("oid2str", oid_str, name);
   if(!state->option_set("str2oid", name))
      state->set_option("str2oid", name, oid_str);
   }

/*************************************************
* Load all of the default OIDs                   *
*************************************************/
void set_default_oids(Library_State* state)
   {
   add_oid(state, "1.2.840.113549.1.1.1", "RSA");
   add_oid(state, "2.5.8.1.1", "RSA");
   add_oid(state, "1.2.840.10040.4.1", "DSA");
   add_oid(state, "1.2.840.10046.2.1", "DH");
   add_oid(state, "1.3.6.1.4.1.3029.1.2.1", "ELG");

   add_oid(state, "1.3.14.3.2.7", "DES/CBC");
   add_oid(state, "1.2.840.113549.3.7", "TripleDES/CBC");
   add_oid(state, "1.2.840.113549.3.2", "RC2/CBC");
   add_oid(state, "1.2.840.113533.7.66.10", "CAST-128/CBC");
   add_oid(state, "2.16.840.1.101.3.4.1.2", "AES-128/CBC");
   add_oid(state, "2.16.840.1.101.3.4.1.22", "AES-192/CBC");
   add_oid(state, "2.16.840.1.101.3.4.1.42", "AES-256/CBC");

   add_oid(state, "1.2.840.113549.2.5", "MD5");
   add_oid(state, "1.3.14.3.2.26", "SHA-160");
   add_oid(state, "1.3.6.1.4.1.11591.12.2", "Tiger(24,3)");

   add_oid(state, "1.2.840.113549.1.9.16.3.6", "KeyWrap.TripleDES");
   add_oid(state, "1.2.840.113549.1.9.16.3.7", "KeyWrap.RC2");
   add_oid(state, "1.2.840.113533.7.66.15", "KeyWrap.CAST-128");
   add_oid(state, "2.16.840.1.101.3.4.1.5", "KeyWrap.AES-128");
   add_oid(state, "2.16.840.1.101.3.4.1.25", "KeyWrap.AES-192");
   add_oid(state, "2.16.840.1.101.3.4.1.45", "KeyWrap.AES-256");

   add_oid(state, "1.2.840.113549.1.9.16.3.8", "Compression.Zlib");

   add_oid(state, "1.2.840.113549.1.1.1", "RSA/EME-PKCS1-v1_5");
   add_oid(state, "1.2.840.113549.1.1.2", "RSA/EMSA3(MD2)");
   add_oid(state, "1.2.840.113549.1.1.4", "RSA/EMSA3(MD5)");
   add_oid(state, "1.2.840.113549.1.1.5", "RSA/EMSA3(SHA-160)");
   add_oid(state, "1.2.840.113549.1.1.11", "RSA/EMSA3(SHA-256)");
   add_oid(state, "1.2.840.113549.1.1.12", "RSA/EMSA3(SHA-384)");
   add_oid(state, "1.2.840.113549.1.1.13", "RSA/EMSA3(SHA-512)");
   add_oid(state, "1.3.36.3.3.1.2", "RSA/EMSA3(RIPEMD-160)");
   add_oid(state, "1.2.840.10040.4.3", "DSA/EMSA1(SHA-160)");

   add_oid(state, "2.5.4.3",  "X520.CommonName");
   add_oid(state, "2.5.4.4",  "X520.Surname");
   add_oid(state, "2.5.4.5",  "X520.SerialNumber");
   add_oid(state, "2.5.4.6",  "X520.Country");
   add_oid(state, "2.5.4.7",  "X520.Locality");
   add_oid(state, "2.5.4.8",  "X520.State");
   add_oid(state, "2.5.4.10", "X520.Organization");
   add_oid(state, "2.5.4.11", "X520.OrganizationalUnit");
   add_oid(state, "2.5.4.12", "X520.Title");
   add_oid(state, "2.5.4.42", "X520.GivenName");
   add_oid(state, "2.5.4.43", "X520.Initials");
   add_oid(state, "2.5.4.44", "X520.GenerationalQualifier");
   add_oid(state, "2.5.4.46", "X520.DNQualifier");
   add_oid(state, "2.5.4.65", "X520.Pseudonym");

   add_oid(state, "1.2.840.113549.1.5.12", "PKCS5.PBKDF2");
   add_oid(state, "1.2.840.113549.1.5.1",  "PBE-PKCS5v15(MD2,DES/CBC)");
   add_oid(state, "1.2.840.113549.1.5.4",  "PBE-PKCS5v15(MD2,RC2/CBC)");
   add_oid(state, "1.2.840.113549.1.5.3",  "PBE-PKCS5v15(MD5,DES/CBC)");
   add_oid(state, "1.2.840.113549.1.5.6",  "PBE-PKCS5v15(MD5,RC2/CBC)");
   add_oid(state, "1.2.840.113549.1.5.10", "PBE-PKCS5v15(SHA-160,DES/CBC)");
   add_oid(state, "1.2.840.113549.1.5.11", "PBE-PKCS5v15(SHA-160,RC2/CBC)");
   add_oid(state, "1.2.840.113549.1.5.13", "PBE-PKCS5v20");

   add_oid(state, "1.2.840.113549.1.9.1", "PKCS9.EmailAddress");
   add_oid(state, "1.2.840.113549.1.9.2", "PKCS9.UnstructuredName");
   add_oid(state, "1.2.840.113549.1.9.3", "PKCS9.ContentType");
   add_oid(state, "1.2.840.113549.1.9.4", "PKCS9.MessageDigest");
   add_oid(state, "1.2.840.113549.1.9.7", "PKCS9.ChallengePassword");
   add_oid(state, "1.2.840.113549.1.9.14", "PKCS9.ExtensionRequest");

   add_oid(state, "1.2.840.113549.1.7.1",      "CMS.DataContent");
   add_oid(state, "1.2.840.113549.1.7.2",      "CMS.SignedData");
   add_oid(state, "1.2.840.113549.1.7.3",      "CMS.EnvelopedData");
   add_oid(state, "1.2.840.113549.1.7.5",      "CMS.DigestedData");
   add_oid(state, "1.2.840.113549.1.7.6",      "CMS.EncryptedData");
   add_oid(state, "1.2.840.113549.1.9.16.1.2", "CMS.AuthenticatedData");
   add_oid(state, "1.2.840.113549.1.9.16.1.9", "CMS.CompressedData");

   add_oid(state, "2.5.29.14", "X509v3.SubjectKeyIdentifier");
   add_oid(state, "2.5.29.15", "X509v3.KeyUsage");
   add_oid(state, "2.5.29.17", "X509v3.SubjectAlternativeName");
   add_oid(state, "2.5.29.18", "X509v3.IssuerAlternativeName");
   add_oid(state, "2.5.29.19", "X509v3.BasicConstraints");
   add_oid(state, "2.5.29.20", "X509v3.CRLNumber");
   add_oid(state, "2.5.29.21", "X509v3.ReasonCode");
   add_oid(state, "2.5.29.23", "X509v3.HoldInstructionCode");
   add_oid(state, "2.5.29.24", "X509v3.InvalidityDate");
   add_oid(state, "2.5.29.32", "X509v3.CertificatePolicies");
   add_oid(state, "2.5.29.35", "X509v3.AuthorityKeyIdentifier");
   add_oid(state, "2.5.29.36", "X509v3.PolicyConstraints");
   add_oid(state, "2.5.29.37", "X509v3.ExtendedKeyUsage");

   add_oid(state, "2.5.29.32.0", "X509v3.AnyPolicy");

   add_oid(state, "1.3.6.1.5.5.7.3.1", "PKIX.ServerAuth");
   add_oid(state, "1.3.6.1.5.5.7.3.2", "PKIX.ClientAuth");
   add_oid(state, "1.3.6.1.5.5.7.3.3", "PKIX.CodeSigning");
   add_oid(state, "1.3.6.1.5.5.7.3.4", "PKIX.EmailProtection");
   add_oid(state, "1.3.6.1.5.5.7.3.5", "PKIX.IPsecEndSystem");
   add_oid(state, "1.3.6.1.5.5.7.3.6", "PKIX.IPsecTunnel");
   add_oid(state, "1.3.6.1.5.5.7.3.7", "PKIX.IPsecUser");
   add_oid(state, "1.3.6.1.5.5.7.3.8", "PKIX.TimeStamping");
   add_oid(state, "1.3.6.1.5.5.7.3.9", "PKIX.OCSPSigning");

   add_oid(state, "1.3.6.1.5.5.7.8.5", "PKIX.XMPPAddr");
   }

/*************************************************
* Set the default algorithm aliases              *
*************************************************/
void set_default_aliases(Library_State* state)
   {
   state->set_option("alias", "OpenPGP.Cipher.1",  "IDEA");
   state->set_option("alias", "OpenPGP.Cipher.2",  "TripleDES");
   state->set_option("alias", "OpenPGP.Cipher.3",  "CAST-128");
   state->set_option("alias", "OpenPGP.Cipher.4",  "Blowfish");
   state->set_option("alias", "OpenPGP.Cipher.5",  "SAFER-SK(13)");
   state->set_option("alias", "OpenPGP.Cipher.7",  "AES-128");
   state->set_option("alias", "OpenPGP.Cipher.8",  "AES-192");
   state->set_option("alias", "OpenPGP.Cipher.9",  "AES-256");
   state->set_option("alias", "OpenPGP.Cipher.10", "Twofish");

   state->set_option("alias", "OpenPGP.Digest.1", "MD5");
   state->set_option("alias", "OpenPGP.Digest.2", "SHA-1");
   state->set_option("alias", "OpenPGP.Digest.3", "RIPEMD-160");
   state->set_option("alias", "OpenPGP.Digest.5", "MD2");
   state->set_option("alias", "OpenPGP.Digest.6", "Tiger(24,3)");
   state->set_option("alias", "OpenPGP.Digest.7", "HAVAL(20,5)");
   state->set_option("alias", "OpenPGP.Digest.8", "SHA-256");

   state->set_option("alias", "TLS.Digest.0",     "Parallel(MD5,SHA-160)");

   state->set_option("alias", "EME-PKCS1-v1_5",  "PKCS1v15");
   state->set_option("alias", "OAEP-MGF1",       "EME1");
   state->set_option("alias", "EME-OAEP",        "EME1");
   state->set_option("alias", "X9.31",           "EMSA2");
   state->set_option("alias", "EMSA-PKCS1-v1_5", "EMSA3");
   state->set_option("alias", "PSS-MGF1",        "EMSA4");
   state->set_option("alias", "EMSA-PSS",        "EMSA4");

   state->set_option("alias", "Rijndael", "AES");
   state->set_option("alias", "3DES",     "TripleDES");
   state->set_option("alias", "DES-EDE",  "TripleDES");
   state->set_option("alias", "CAST5",    "CAST-128");
   state->set_option("alias", "SHA1",     "SHA-160");
   state->set_option("alias", "SHA-1",    "SHA-160");
   state->set_option("alias", "SEAL",     "SEAL-3.0-BE");
   state->set_option("alias", "MARK-4",   "ARC4(256)");
   state->set_option("alias", "OMAC",     "CMAC");
   }

/*************************************************
* Set the default configuration toggles          *
*************************************************/
void set_default_config(Library_State* state)
   {
   state->set_option("conf", "base/memory_chunk", "64*1024");
   state->set_option("conf", "base/pkcs8_tries", "3");
   state->set_option("conf", "base/default_pbe",
                     "PBE-PKCS5v20(SHA-1,TripleDES/CBC)");
   state->set_option("conf", "base/default_allocator", "malloc");

   state->set_option("conf", "pk/blinder_size", "64");
   state->set_option("conf", "pk/test/public", "basic");
   state->set_option("conf", "pk/test/private", "basic");
   state->set_option("conf", "pk/test/private_gen", "all");

   state->set_option("conf", "pem/search", "4*1024");
   state->set_option("conf", "pem/forgive", "8");
   state->set_option("conf", "pem/width", "64");

   state->set_option("conf", "rng/min_entropy", "256", false);
   state->set_option("conf", "rng/ms_capi_prov_type", "INTEL_SEC:RSA_FULL");
   state->set_option("conf", "rng/unix_path", "/usr/ucb:/usr/etc:/etc");
   state->set_option("conf", "rng/es_files", "/dev/urandom:/dev/random");
   state->set_option("conf", "rng/egd_path",
                     "/var/run/egd-pool:/dev/egd-pool");

   state->set_option("conf", "x509/validity_slack", "24h");
   state->set_option("conf", "x509/v1_assume_ca", "false");
   state->set_option("conf", "x509/cache_verify_results", "30m");

   state->set_option("conf", "x509/ca/allow_ca", "false");
   state->set_option("conf", "x509/ca/basic_constraints", "always");
   state->set_option("conf", "x509/ca/default_expire", "1y");
   state->set_option("conf", "x509/ca/signing_offset", "30s");
   state->set_option("conf", "x509/ca/rsa_hash", "SHA-1");
   state->set_option("conf", "x509/ca/str_type", "latin1");

   state->set_option("conf", "x509/crl/unknown_critical", "ignore");
   state->set_option("conf", "x509/crl/next_update", "7d");

   state->set_option("conf", "x509/exts/basic_constraints", "critical");
   state->set_option("conf", "x509/exts/subject_key_id", "yes");
   state->set_option("conf", "x509/exts/authority_key_id", "yes");
   state->set_option("conf", "x509/exts/subject_alternative_name", "yes");
   state->set_option("conf", "x509/exts/issuer_alternative_name", "yes");
   state->set_option("conf", "x509/exts/key_usage", "critical");
   state->set_option("conf", "x509/exts/extended_key_usage", "yes");
   state->set_option("conf", "x509/exts/crl_number", "yes");
   }

/*************************************************
* Set the built-in discrete log groups           *
*************************************************/
void set_default_dl_groups(Library_State* state)
   {
   state->set_option("dl", "modp/ietf/768",
      "-----BEGIN X942 DH PARAMETERS-----"
      "MIHIAmEA///////////JD9qiIWjCNMTGYouA3BzRKQJOCIpnzHQCC76mOxObIlFK"
      "CHmONATd75UZs806QxswKwpt8l8UN0/hNW1tUcJF5IW1dmJefsb0TELppjo2IP//"
      "////////AgECAmB//////////+SH7VEQtGEaYmMxRcBuDmiUgScERTPmOgEF31Md"
      "ic2RKKUEPMcaAm73yozZ5p0hjZgVhTb5L4obp/Catrao4SLyQtq7MS8/Y3omIXTT"
      "HRsQf/////////8="
      "-----END X942 DH PARAMETERS-----");

   state->set_option("dl", "modp/ietf/1024",
      "-----BEGIN X942 DH PARAMETERS-----"
      "MIIBCgKBgQD//////////8kP2qIhaMI0xMZii4DcHNEpAk4IimfMdAILvqY7E5si"
      "UUoIeY40BN3vlRmzzTpDGzArCm3yXxQ3T+E1bW1RwkXkhbV2Yl5+xvRMQummN+1r"
      "C/9ctvQGt+3uOGv7Womfpa6fJBF8Sx/mSShmUezmU4H//////////wIBAgKBgH//"
      "////////5IftURC0YRpiYzFFwG4OaJSBJwRFM+Y6AQXfUx2JzZEopQQ8xxoCbvfK"
      "jNnmnSGNmBWFNvkvihun8Jq2tqjhIvJC2rsxLz9jeiYhdNMb9rWF/65begNb9vcc"
      "Nf2tRM/S10+SCL4lj/MklDMo9nMpwP//////////"
      "-----END X942 DH PARAMETERS-----");

   state->set_option("dl", "modp/ietf/1536",
      "-----BEGIN X942 DH PARAMETERS-----"
      "MIIBigKBwQD//////////8kP2qIhaMI0xMZii4DcHNEpAk4IimfMdAILvqY7E5si"
      "UUoIeY40BN3vlRmzzTpDGzArCm3yXxQ3T+E1bW1RwkXkhbV2Yl5+xvRMQummN+1r"
      "C/9ctvQGt+3uOGv7Womfpa6fJBF8Sx/mSShmUezkWz3CAHy4oWO/BZjaSDYcVdOa"
      "aRY/qP0kz1+DZV0j3KOtlhxi81YghVK7ntUpB3CWlm1nDDVOSryYBPF0bAjKI3Mn"
      "//////////8CAQICgcB//////////+SH7VEQtGEaYmMxRcBuDmiUgScERTPmOgEF"
      "31Mdic2RKKUEPMcaAm73yozZ5p0hjZgVhTb5L4obp/Catrao4SLyQtq7MS8/Y3om"
      "IXTTG/a1hf+uW3oDW/b3HDX9rUTP0tdPkgi+JY/zJJQzKPZyLZ7hAD5cULHfgsxt"
      "JBsOKunNNIsf1H6SZ6/Bsq6R7lHWyw4xeasQQqldz2qUg7hLSzazhhqnJV5MAni6"
      "NgRlEbmT//////////8="
      "-----END X942 DH PARAMETERS-----");

   state->set_option("dl", "modp/ietf/2048",
      "-----BEGIN X942 DH PARAMETERS-----"
      "MIICDAKCAQEA///////////JD9qiIWjCNMTGYouA3BzRKQJOCIpnzHQCC76mOxOb"
      "IlFKCHmONATd75UZs806QxswKwpt8l8UN0/hNW1tUcJF5IW1dmJefsb0TELppjft"
      "awv/XLb0Brft7jhr+1qJn6WunyQRfEsf5kkoZlHs5Fs9wgB8uKFjvwWY2kg2HFXT"
      "mmkWP6j9JM9fg2VdI9yjrZYcYvNWIIVSu57VKQdwlpZtZww1Tkq8mATxdGwIyhgh"
      "fDKQXkYuNs474553LBgOhgObJ4Oi7Aeij7XFXfBvTFLJ3ivL9pVYFxg5lUl86pVq"
      "5RXSJhiY+gUQFXKOWoqsqmj//////////wIBAgKCAQB//////////+SH7VEQtGEa"
      "YmMxRcBuDmiUgScERTPmOgEF31Mdic2RKKUEPMcaAm73yozZ5p0hjZgVhTb5L4ob"
      "p/Catrao4SLyQtq7MS8/Y3omIXTTG/a1hf+uW3oDW/b3HDX9rUTP0tdPkgi+JY/z"
      "JJQzKPZyLZ7hAD5cULHfgsxtJBsOKunNNIsf1H6SZ6/Bsq6R7lHWyw4xeasQQqld"
      "z2qUg7hLSzazhhqnJV5MAni6NgRlDBC+GUgvIxcbZx3xzzuWDAdDAc2TwdF2A9FH"
      "2uKu+DemKWTvFeX7SqwLjBzKpL51SrVyiukTDEx9AogKuUctRVZVNH//////////"
      "-----END X942 DH PARAMETERS-----");

   state->set_option("dl", "modp/ietf/3072",
      "-----BEGIN X942 DH PARAMETERS-----"
      "MIIDDAKCAYEA///////////JD9qiIWjCNMTGYouA3BzRKQJOCIpnzHQCC76mOxOb"
      "IlFKCHmONATd75UZs806QxswKwpt8l8UN0/hNW1tUcJF5IW1dmJefsb0TELppjft"
      "awv/XLb0Brft7jhr+1qJn6WunyQRfEsf5kkoZlHs5Fs9wgB8uKFjvwWY2kg2HFXT"
      "mmkWP6j9JM9fg2VdI9yjrZYcYvNWIIVSu57VKQdwlpZtZww1Tkq8mATxdGwIyhgh"
      "fDKQXkYuNs474553LBgOhgObJ4Oi7Aeij7XFXfBvTFLJ3ivL9pVYFxg5lUl86pVq"
      "5RXSJhiY+gUQFXKOWoqqxC2tMxcNBFB6M6hVIavfHLpk7PuFBFjb7wqK6nFXXQYM"
      "fbOXD4Wm4eTHq/WujNsJM9cejJTgSiVhnc7j0iYa0u5r8S/6BtmKCGTYdgJzPshq"
      "ZFIfKxgXeyAMu+EXV3phXWx3CYjAutlG4gjiT6B05asxQ9tb/OD9EI5LgtEgqTrS"
      "yv//////////AgECAoIBgH//////////5IftURC0YRpiYzFFwG4OaJSBJwRFM+Y6"
      "AQXfUx2JzZEopQQ8xxoCbvfKjNnmnSGNmBWFNvkvihun8Jq2tqjhIvJC2rsxLz9j"
      "eiYhdNMb9rWF/65begNb9vccNf2tRM/S10+SCL4lj/MklDMo9nItnuEAPlxQsd+C"
      "zG0kGw4q6c00ix/UfpJnr8GyrpHuUdbLDjF5qxBCqV3PapSDuEtLNrOGGqclXkwC"
      "eLo2BGUMEL4ZSC8jFxtnHfHPO5YMB0MBzZPB0XYD0Ufa4q74N6YpZO8V5ftKrAuM"
      "HMqkvnVKtXKK6RMMTH0CiAq5Ry1FVWIW1pmLhoIoPRnUKpDV745dMnZ9woIsbfeF"
      "RXU4q66DBj7Zy4fC03DyY9X610ZthJnrj0ZKcCUSsM7ncekTDWl3NfiX/QNsxQQy"
      "bDsBOZ9kNTIpD5WMC72QBl3wi6u9MK62O4TEYF1so3EEcSfQOnLVmKHtrf5wfohH"
      "JcFokFSdaWV//////////w=="
      "-----END X942 DH PARAMETERS-----");

   state->set_option("dl", "modp/ietf/4096",
      "-----BEGIN X942 DH PARAMETERS-----"
      "MIIEDAKCAgEA///////////JD9qiIWjCNMTGYouA3BzRKQJOCIpnzHQCC76mOxOb"
      "IlFKCHmONATd75UZs806QxswKwpt8l8UN0/hNW1tUcJF5IW1dmJefsb0TELppjft"
      "awv/XLb0Brft7jhr+1qJn6WunyQRfEsf5kkoZlHs5Fs9wgB8uKFjvwWY2kg2HFXT"
      "mmkWP6j9JM9fg2VdI9yjrZYcYvNWIIVSu57VKQdwlpZtZww1Tkq8mATxdGwIyhgh"
      "fDKQXkYuNs474553LBgOhgObJ4Oi7Aeij7XFXfBvTFLJ3ivL9pVYFxg5lUl86pVq"
      "5RXSJhiY+gUQFXKOWoqqxC2tMxcNBFB6M6hVIavfHLpk7PuFBFjb7wqK6nFXXQYM"
      "fbOXD4Wm4eTHq/WujNsJM9cejJTgSiVhnc7j0iYa0u5r8S/6BtmKCGTYdgJzPshq"
      "ZFIfKxgXeyAMu+EXV3phXWx3CYjAutlG4gjiT6B05asxQ9tb/OD9EI5LgtEgqSEI"
      "ARpyPBKnh+bXiHGaEL26WyaZwycYavTiPBqUaDS2FQvaJYPpyirUTOjbu8LbBN6O"
      "+S6O/BQfvsqmKHxZR05rwF2ZspZPoJDDoiM7oYZRW+ftH2EpcM7i16+4G912IXBI"
      "HNAGkSfVsFqpk7TqmI2P3cGG/7fckKbAj030Nck0BjGZ//////////8CAQICggIA"
      "f//////////kh+1RELRhGmJjMUXAbg5olIEnBEUz5joBBd9THYnNkSilBDzHGgJu"
      "98qM2eadIY2YFYU2+S+KG6fwmra2qOEi8kLauzEvP2N6JiF00xv2tYX/rlt6A1v2"
      "9xw1/a1Ez9LXT5IIviWP8ySUMyj2ci2e4QA+XFCx34LMbSQbDirpzTSLH9R+kmev"
      "wbKuke5R1ssOMXmrEEKpXc9qlIO4S0s2s4YapyVeTAJ4ujYEZQwQvhlILyMXG2cd"
      "8c87lgwHQwHNk8HRdgPRR9rirvg3pilk7xXl+0qsC4wcyqS+dUq1corpEwxMfQKI"
      "CrlHLUVVYhbWmYuGgig9GdQqkNXvjl0ydn3Cgixt94VFdTirroMGPtnLh8LTcPJj"
      "1frXRm2EmeuPRkpwJRKwzudx6RMNaXc1+Jf9A2zFBDJsOwE5n2Q1MikPlYwLvZAG"
      "XfCLq70wrrY7hMRgXWyjcQRxJ9A6ctWYoe2t/nB+iEclwWiQVJCEAI05HglTw/Nr"
      "xDjNCF7dLZNM4ZOMNXpxHg1KNBpbCoXtEsH05RVqJnRt3eFtgm9HfJdHfgoP32VT"
      "FD4so6c14C7M2Usn0Ehh0RGd0MMorfP2j7CUuGdxa9fcDe67ELgkDmgDSJPq2C1U"
      "ydp1TEbH7uDDf9vuSFNgR6b6GuSaAxjM//////////8="
      "-----END X942 DH PARAMETERS-----");

   state->set_option("dl", "dsa/jce/512",
      "-----BEGIN DSA PARAMETERS-----"
      "MIGdAkEA/KaCzo4Syrom78z3EQ5SbbB4sF7ey80etKII864WF64B81uRpH5t9jQT"
      "xeEu0ImbzRMqzVDZkVG9xD7nN1kuFwIVAJYu3cw2nLqOuyYO5rahJtk0bjjFAkEA"
      "3gtU76vylwh+5iPVylWIxkgo70/eT/uuHs0gBndrBbEbgeo83pvDlkwWh8UyW/Q9"
      "fM76DQqGvl3/3dDRFD3NdQ=="
      "-----END DSA PARAMETERS-----");

   state->set_option("dl", "dsa/jce/768",
      "-----BEGIN DSA PARAMETERS-----"
      "MIHdAmEA6eZCWZ01XzfJf/01ZxILjiXJzUPpJ7OpZw++xdiQFBki0sOzrSSACTeZ"
      "hp0ehGqrSfqwrSbSzmoiIZ1HC859d31KIfvpwnC1f2BwAvPO+Dk2lM9F7jaIwRqM"
      "VqsSej2vAhUAnNvYTJ8awvOND4D0KrlS5zOL9RECYQDe7p717RUWzn5pXmcrjO5F"
      "5s17NuDmOF+JS6hhY/bz5sbU6KgRRtQBfe/dccvZD6Akdlm4i3zByJT0gmn9Txqs"
      "CjBTjf9rP8ds+xMcnnlltYhYqwpDtVczWRKoqlR/lWg="
      "-----END DSA PARAMETERS-----");

   state->set_option("dl", "dsa/jce/1024",
      "-----BEGIN DSA PARAMETERS-----"
      "MIIBHgKBgQD9f1OBHXUSKVLfSpwu7OTn9hG3UjzvRADDHj+AtlEmaUVdQCJR+1k9"
      "jVj6v8X1ujD2y5tVbNeBO4AdNG/yZmC3a5lQpaSfn+gEexAiwk+7qdf+t8Yb+DtX"
      "58aophUPBPuD9tPFHsMCNVQTWhaRMvZ1864rYdcq7/IiAxmd0UgBxwIVAJdgUI8V"
      "IwvMspK5gqLrhAvwWBz1AoGARpYDUS4wJ4zTlHWV2yLuyYJqYyKtyXNE9B10DDJX"
      "JMj577qn1NgD/4xgnc0QDrxb38+tfGpCX66nhuogUOvpg1HqH9of3yTWlHqmuaoj"
      "dmlTgC9NfUqOy6BtGXaKJJH/sW0O+cQ6mbX3FnL/bwoktETQc20E04oaEyLa9s3Y"
      "jJ0="
      "-----END DSA PARAMETERS-----");
   }

}

/*************************************************
* Set the default policy                         *
*************************************************/
void Library_State::set_default_policy()
   {
   set_default_config(this);
   set_default_aliases(this);
   set_default_oids(this);
   set_default_dl_groups(this);
   }

}
