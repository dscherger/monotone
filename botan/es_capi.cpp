/*************************************************
* Win32 CAPI EntropySource Source File           *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/es_capi.h>
#include <botan/conf.h>
#include <windows.h>
#include <wincrypt.h>

namespace Botan {

namespace {

/*************************************************
* CSP Handle                                     *
*************************************************/
class CSP_Handle
   {
   public:
      CSP_Handle(u64bit);
      ~CSP_Handle();

      bool is_valid() const { return valid; }

      HCRYPTPROV get_handle() const { return handle; }
   private:
      HCRYPTPROV handle;
      bool valid;
   };

/*************************************************
* Initialize a CSP Handle                        *
*************************************************/
CSP_Handle::CSP_Handle(u64bit capi_provider)
   {
   valid = false;
   DWORD prov_type = (DWORD)capi_provider;

   if(CryptAcquireContext(&handle, 0, 0, prov_type, CRYPT_VERIFYCONTEXT))
      valid = true;
   }

/*************************************************
* Destroy a CSP Handle                           *
*************************************************/
CSP_Handle::~CSP_Handle()
   {
   if(valid)
      CryptReleaseContext(handle, 0);
   }

}

/*************************************************
* Gather Entropy from Win32 CAPI                 *
*************************************************/
u32bit Win32_CAPI_EntropySource::slow_poll(byte output[], u32bit length)
   {
   if(length > 64)
      length = 64;

   for(u32bit j = 0; j != prov_types.size(); j++)
      {
      CSP_Handle csp(prov_types[j]);
      if(!csp.is_valid()) continue;
      if(CryptGenRandom(csp.get_handle(), length, output)) break;
      }
   return length;
   }

/*************************************************
* Gather Entropy from Win32 CAPI                 *
*************************************************/
Win32_CAPI_EntropySource::Win32_CAPI_EntropySource(const std::string& provs)
   {
   std::vector<std::string> capi_provs;

   if(provs == "")
      capi_provs = Config::get_list("rng/ms_capi_prov_type");
   else
      capi_provs = split_on(provs, ':');

   for(u32bit j = 0; j != capi_provs.size(); j++)
      {
      if(capi_provs[j] == "RSA_FULL")  prov_types.push_back(PROV_RSA_FULL);
      if(capi_provs[j] == "INTEL_SEC") prov_types.push_back(PROV_INTEL_SEC);
      if(capi_provs[j] == "FORTEZZA")  prov_types.push_back(PROV_FORTEZZA);
      if(capi_provs[j] == "RNG")       prov_types.push_back(PROV_RNG);
      }

   if(prov_types.size() == 0)
      prov_types.push_back(PROV_RSA_FULL);
   }

}
