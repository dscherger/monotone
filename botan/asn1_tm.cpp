/*************************************************
* X.509 Time Types Source File                   *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/asn1_obj.h>
#include <botan/conf.h>
#include <ctime>

namespace Botan {

namespace {

/*************************************************
* Convert a time_t to a struct tm                *
*************************************************/
std::tm get_tm(u64bit timer)
   {
   std::time_t time_val = (std::time_t)timer;

   if((u64bit)time_val != timer)
      throw Encoding_Error("X509_Time: time_t overflow with time value " +
                           to_string(timer));

   std::tm* tm_p = std::gmtime(&time_val);
   if(tm_p == 0)
      throw Encoding_Error("X509_Time: gmtime could not encode " +
                           to_string(timer));
   return (*tm_p);
   }

}

/*************************************************
* Create an X509_Time                            *
*************************************************/
X509_Time::X509_Time(const std::string& time_str)
   {
   if(time_str == "")
      {
      year = month = day = hour = minute = second = 0;
      return;
      }

   std::vector<std::string> params;
   std::string current;

   for(u32bit j = 0; j != time_str.size(); j++)
      {
      if(is_digit(time_str[j]))
         current += time_str[j];
      else
         {
         if(current != "")
            params.push_back(current);
         current = "";
         }
      }
   if(current != "")
      params.push_back(current);

   if(params.size() < 3 || params.size() > 6)
      throw Invalid_Argument("Invalid time specification " + time_str);

   year   = to_u32bit(params[0]);
   month  = to_u32bit(params[1]);
   day    = to_u32bit(params[2]);
   hour   = (params.size() >= 4) ? to_u32bit(params[3]) : 0;
   minute = (params.size() >= 5) ? to_u32bit(params[4]) : 0;
   second = (params.size() == 6) ? to_u32bit(params[5]) : 0;

   if(year >= 2050)
      tag = GENERALIZED_TIME;
   else
      tag = UTC_TIME;

   if(!passes_sanity_check())
      throw Invalid_Argument("Invalid time specification " + time_str);
   }

/*************************************************
* Create an X509_Time                            *
*************************************************/
X509_Time::X509_Time(u64bit timer)
   {
   std::tm time_info = get_tm(timer);

   year   = time_info.tm_year + 1900;
   month  = time_info.tm_mon + 1;
   day    = time_info.tm_mday;
   hour   = time_info.tm_hour;
   minute = time_info.tm_min;
   second = time_info.tm_sec;

   if(year >= 2050)
      tag = GENERALIZED_TIME;
   else
      tag = UTC_TIME;
   }

/*************************************************
* Create an X509_Time                            *
*************************************************/
X509_Time::X509_Time(const std::string& t_spec, ASN1_Tag t) : tag(t)
   {
   if(tag != GENERALIZED_TIME && tag != UTC_TIME)
      throw Invalid_Argument("X509_Time: Invalid tag " + to_string(tag));
   if(tag == GENERALIZED_TIME && t_spec.size() != 13 && t_spec.size() != 15)
      throw Invalid_Argument("Invalid GeneralizedTime: " + t_spec);
   if(tag == UTC_TIME && t_spec.size() != 11 && t_spec.size() != 13)
      throw Invalid_Argument("Invalid UTCTime: " + t_spec);
   if(t_spec[t_spec.size()-1] != 'Z')
      throw Invalid_Argument("Invalid time encoding: " + t_spec);

   const u32bit YEAR_SIZE = (tag == UTC_TIME) ? 2 : 4;

   std::vector<std::string> params;
   std::string current;

   for(u32bit j = 0; j != YEAR_SIZE; j++)
      current += t_spec[j];
   params.push_back(current);
   current = "";

   for(u32bit j = YEAR_SIZE; j != t_spec.size() - 1; j++)
      {
      current += t_spec[j];
      if(current.size() == 2)
         {
         params.push_back(current);
         current = "";
         }
      }

   year   = to_u32bit(params[0]);
   month  = to_u32bit(params[1]);
   day    = to_u32bit(params[2]);
   hour   = to_u32bit(params[3]);
   minute = to_u32bit(params[4]);
   second = (params.size() == 6) ? to_u32bit(params[5]) : 0;

   if(tag == UTC_TIME)
      {
      if(year >= 50) year += 1900;
      else           year += 2000;
      }

   if(!passes_sanity_check())
      throw Invalid_Argument("Invalid time specification " + t_spec);
   }

/*************************************************
* Return a string representation of the time     *
*************************************************/
std::string X509_Time::as_string() const
   {
   if(time_is_set() == false)
      throw Invalid_State("X509_Time::as_string: No time set");

   std::string asn1rep;
   if(tag == GENERALIZED_TIME)
      asn1rep = to_string(year, 4);
   else
      {
      if(year < 1950 || year >= 2050)
         throw Encoding_Error("X509_Time: The time " + readable_string() +
                              " cannot be encoded as a UTCTime");
      u32bit asn1year = (year >= 2000) ? (year - 2000) : (year - 1900);
      asn1rep = to_string(asn1year, 2);
      }
   asn1rep += to_string(month, 2) + to_string(day, 2);
   asn1rep += to_string(hour, 2) + to_string(minute, 2) + to_string(second, 2);
   asn1rep += "Z";
   return asn1rep;
   }

/*************************************************
* Return if the time has been set somehow        *
*************************************************/
bool X509_Time::time_is_set() const
   {
   return (year != 0);
   }

/*************************************************
* Return a human readable string representation  *
*************************************************/
std::string X509_Time::readable_string() const
   {
   if(time_is_set() == false)
      throw Invalid_State("X509_Time::readable_string: No time set");

   std::string readable;
   readable += to_string(year,   4) + "/";
   readable += to_string(month    ) + "/";
   readable += to_string(day      ) + " ";
   readable += to_string(hour     ) + ":";
   readable += to_string(minute, 2) + ":";
   readable += to_string(second, 2) + " UTC";
   return readable;
   }

/*************************************************
* Do a general sanity check on the time          *
*************************************************/
bool X509_Time::passes_sanity_check() const
   {
   if(year < 1950 || year > 2100)
      return false;
   if(month == 0 || month > 12)
      return false;
   if(day == 0 || day > 31)
      return false;
   if(hour >= 24 || minute > 60 || second > 60)
      return false;
   return true;
   }

/*************************************************
* Return the type of this Time object            *
*************************************************/
ASN1_Tag X509_Time::tagging() const
   {
   return tag;
   }

/*************************************************
* Compare this time against another              *
*************************************************/
s32bit X509_Time::cmp(const X509_Time& other) const
   {
   if(time_is_set() == false)
      throw Invalid_State("X509_Time::cmp: No time set");

   const s32bit EARLIER = -1, LATER = 1, SAME_TIME = 0;

   if(year < other.year)     return EARLIER;
   if(year > other.year)     return LATER;
   if(month < other.month)   return EARLIER;
   if(month > other.month)   return LATER;
   if(day < other.day)       return EARLIER;
   if(day > other.day)       return LATER;
   if(hour < other.hour)     return EARLIER;
   if(hour > other.hour)     return LATER;
   if(minute < other.minute) return EARLIER;
   if(minute > other.minute) return LATER;
   if(second < other.second) return EARLIER;
   if(second > other.second) return LATER;

   return SAME_TIME;
   }

/*************************************************
* Compare this time against another              *
*************************************************/
s32bit X509_Time::cmp(u64bit seconds) const
   {
   return cmp(X509_Time(seconds));
   }

/*************************************************
* Compare two X509_Times for in various ways     *
*************************************************/
bool operator==(const X509_Time& t1, const X509_Time& t2)
   { return (t1.cmp(t2) == 0); }
bool operator!=(const X509_Time& t1, const X509_Time& t2)
   { return (t1.cmp(t2) != 0); }
bool operator<=(const X509_Time& t1, const X509_Time& t2)
   { return (t1.cmp(t2) <= 0); }
bool operator>=(const X509_Time& t1, const X509_Time& t2)
   { return (t1.cmp(t2) >= 0); }

/*************************************************
* Do a validity check                            *
*************************************************/
s32bit validity_check(const X509_Time& start, const X509_Time& end,
                      u64bit current_time)
   {
   const u32bit ALLOWABLE_SLIP = Config::get_time("x509/validity_slack");
   const s32bit NOT_YET_VALID = -1, VALID_TIME = 0, EXPIRED = 1;

   if(start.cmp(current_time + ALLOWABLE_SLIP) > 0)
      return NOT_YET_VALID;
   if(end.cmp(current_time - ALLOWABLE_SLIP) < 0)
      return EXPIRED;
   return VALID_TIME;
   }

namespace DER {

/*************************************************
* DER encode an X509_Time                        *
*************************************************/
void encode(DER_Encoder& encoder, const X509_Time& obj, ASN1_Tag tagging)
   {
   if(tagging != GENERALIZED_TIME && tagging != UTC_TIME)
      throw Invalid_Argument("DER::encode: Bad encoding tag for time value");
   encoder.add_object(tagging, UNIVERSAL, local2iso(obj.as_string()));
   }

/*************************************************
* DER encode an X509_Time                        *
*************************************************/
void encode(DER_Encoder& encoder, const X509_Time& obj)
   {
   DER::encode(encoder, obj, obj.tagging());
   }

}

namespace BER {

/*************************************************
* Decode a BER encoded X509_Time                 *
*************************************************/
void decode(BER_Decoder& source, X509_Time& obj)
   {
   BER_Object ber_obj = source.get_next_object();
   obj = X509_Time(iso2local(BER::to_string(ber_obj)), ber_obj.type_tag);
   }

}

}
