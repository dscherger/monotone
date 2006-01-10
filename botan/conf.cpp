/*************************************************
* Configuration Handling Source File             *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/conf.h>
#include <botan/lookup.h>
#include <botan/mutex.h>
#include <botan/charset.h>
#include <botan/parsing.h>
#include <string>
#include <map>

namespace Botan {

namespace {

/*************************************************
* Holder for name-value option pairs             *
*************************************************/
class Options
   {
   public:
      std::string get(const std::string&);
      void set(const std::string&, const std::string&, bool);

      Options() { options_mutex = get_mutex(); }
      ~Options() { delete options_mutex; }
   private:
      std::map<std::string, std::string> options;
      Mutex* options_mutex;
   };

/*************************************************
* Get an option by name                          *
*************************************************/
std::string Options::get(const std::string& name)
   {
   Mutex_Holder lock(options_mutex);

   std::map<std::string, std::string>::const_iterator i = options.find(name);
   if(i != options.end())
      return i->second;
   return "";
   }

/*************************************************
* Set an option by name                          *
*************************************************/
void Options::set(const std::string& name, const std::string& value,
                  bool overwrite)
   {
   const bool have_it = ((get(name) == "") ? false : true);

   Mutex_Holder lock(options_mutex);
   if(overwrite || !have_it)
      options[name] = value;
   }

/*************************************************
* Global state                                   *
*************************************************/
Options* options = 0;

}

namespace Init {

/*************************************************
* Startup the configuration system               *
*************************************************/
void startup_conf()
   {
   options = new Options;
   }

/*************************************************
* Shutdown the configuration system              *
*************************************************/
void shutdown_conf()
   {
   delete options;
   options = 0;
   }

}

namespace Config {

/*************************************************
* Set an option                                  *
*************************************************/
void set(const std::string& name, const std::string& value, bool overwrite)
   {
   if(!options)
      throw Internal_Error("Config::set: Conf system never started");

   options->set(name, value, overwrite);
   }

/*************************************************
* Get the value of an option as a string         *
*************************************************/
std::string get_string(const std::string& name)
   {
   if(!options)
      throw Internal_Error("Config::get: Conf system never started");

   return options->get(name);
   }

/*************************************************
* Get the value as a list of strings             *
*************************************************/
std::vector<std::string> get_list(const std::string& name)
   {
   return split_on(get_string(name), ':');
   }

/*************************************************
* Get the value as a u32bit                      *
*************************************************/
u32bit get_u32bit(const std::string& name)
   {
   return parse_expr(get_string(name));
   }

/*************************************************
* Get the value as a time                        *
*************************************************/
u32bit get_time(const std::string& name)
   {
   const std::string timespec = get_string(name);
   if(timespec == "")
      return 0;

   const char suffix = timespec[timespec.size()-1];
   std::string value = timespec.substr(0, timespec.size()-1);

   u32bit scale = 1;

   if(is_digit(suffix))
      value += suffix;
   else if(suffix == 's')
      scale = 1;
   else if(suffix == 'm')
      scale = 60;
   else if(suffix == 'h')
      scale = 60 * 60;
   else if(suffix == 'd')
      scale = 24 * 60 * 60;
   else if(suffix == 'y')
      scale = 365 * 24 * 60 * 60;
   else
      throw Decoding_Error("Config::get_time: Unknown time value " + value);

   return scale * to_u32bit(value);
   }

/*************************************************
* Get the value as a boolean                     *
*************************************************/
bool get_bool(const std::string& name)
   {
   const std::string value = get_string(name);
   if(value == "0" || value == "false")
      return false;
   if(value == "1" || value == "true")
      return true;
   throw Decoding_Error("Config::get_bool: Unknown boolean value " + value);
   }

/*************************************************
* Choose the signature format for a PK algorithm *
*************************************************/
void choose_sig_format(const std::string& algo_name, std::string& padding,
                       Signature_Format& format)
   {
   std::string dummy;
   choose_sig_format(algo_name, padding, dummy, format);
   }

/*************************************************
* Choose the signature format for a PK algorithm *
*************************************************/
void choose_sig_format(const std::string& algo_name, std::string& padding,
                       std::string& hash, Signature_Format& format)
   {
   if(algo_name == "RSA")
      {
      hash = deref_alias(get_string("x509/ca/rsa_hash"));
      if(hash == "")
         throw Invalid_State("No value set for x509/ca/rsa_hash");

      padding = "EMSA3(" + hash + ")";
      format = IEEE_1363;
      }
   else if(algo_name == "DSA")
      {
      hash = deref_alias("SHA-1");
      padding = "EMSA1(" + hash + ")";
      format = DER_SEQUENCE;
      }
   else
      throw Invalid_Argument("Unknown X.509 signing key type: " + algo_name);
   }

}

}
