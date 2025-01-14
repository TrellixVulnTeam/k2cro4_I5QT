// Copyright 2012 the v8-i18n authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/date-format.h"

#include <string.h>

#include "src/utils.h"
#include "unicode/calendar.h"
#include "unicode/dtfmtsym.h"
#include "unicode/dtptngen.h"
#include "unicode/locid.h"
#include "unicode/numsys.h"
#include "unicode/smpdtfmt.h"
#include "unicode/timezone.h"

namespace v8_i18n {

static icu::SimpleDateFormat* InitializeDateTimeFormat(v8::Handle<v8::String>,
                                                       v8::Handle<v8::Object>,
                                                       v8::Handle<v8::Object>);
static icu::SimpleDateFormat* CreateICUDateFormat(const icu::Locale&,
                                                  v8::Handle<v8::Object>);
static void SetResolvedSettings(const icu::Locale&,
                                icu::SimpleDateFormat*,
                                v8::Handle<v8::Object>);

icu::SimpleDateFormat* DateFormat::UnpackDateFormat(
    v8::Handle<v8::Object> obj) {
  v8::HandleScope handle_scope;

  if (obj->HasOwnProperty(v8::String::New("dateFormat"))) {
    return static_cast<icu::SimpleDateFormat*>(
        obj->GetAlignedPointerFromInternalField(0));
  }

  return NULL;
}

void DateFormat::DeleteDateFormat(v8::Persistent<v8::Value> object,
                                  void* param) {
  v8::Persistent<v8::Object> persistent_object =
      v8::Persistent<v8::Object>::Cast(object);

  // First delete the hidden C++ object.
  // Unpacking should never return NULL here. That would only happen if
  // this method is used as the weak callback for persistent handles not
  // pointing to a date time formatter.
  delete UnpackDateFormat(persistent_object);

  // Then dispose of the persistent handle to JS object.
  persistent_object.Dispose();
}

v8::Handle<v8::Value> DateFormat::JSInternalFormat(
    const v8::Arguments& args) {
  v8::HandleScope handle_scope;

  double millis = 0.0;
  if (args.Length() != 2 || !args[0]->IsObject() || !args[1]->IsDate()) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New(
            "Internal error. Formatter and date value have to be specified.")));
  } else {
    millis = v8::Date::Cast(*args[1])->NumberValue();
  }

  icu::SimpleDateFormat* date_format = UnpackDateFormat(args[0]->ToObject());
  if (!date_format) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("DateTimeFormat method called on an object "
                        "that is not a DateTimeFormat.")));
  }

  icu::UnicodeString result;
  date_format->format(millis, result);

  return v8::String::New(
      reinterpret_cast<const uint16_t*>(result.getBuffer()), result.length());
}

v8::Handle<v8::Value> DateFormat::JSInternalParse(
    const v8::Arguments& args) {
  v8::HandleScope handle_scope;

  icu::UnicodeString string_date;
  if (args.Length() != 2 || !args[0]->IsObject() || !args[1]->IsString()) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New(
            "Internal error. Formatter and string have to be specified.")));
  } else {
    if (!Utils::V8StringToUnicodeString(args[1], &string_date)) {
      string_date = "";
    }
  }

  icu::SimpleDateFormat* date_format = UnpackDateFormat(args[0]->ToObject());
  if (!date_format) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("DateTimeFormat method called on an object "
                        "that is not a DateTimeFormat.")));
  }

  UErrorCode status = U_ZERO_ERROR;
  UDate date = date_format->parse(string_date, status);
  if (U_FAILURE(status)) {
    return v8::Undefined();
  }

  return v8::Date::New(static_cast<double>(date));
}

v8::Handle<v8::Value> DateFormat::JSCreateDateTimeFormat(
    const v8::Arguments& args) {
  v8::HandleScope handle_scope;

  if (args.Length() != 3 ||
      !args[0]->IsString() ||
      !args[1]->IsObject() ||
      !args[2]->IsObject()) {
    return v8::ThrowException(v8::Exception::Error(
        v8::String::New("Internal error, wrong parameters.")));
  }

  v8::Persistent<v8::ObjectTemplate> date_format_template =
      Utils::GetTemplate();

  // Create an empty object wrapper.
  v8::Local<v8::Object> local_object = date_format_template->NewInstance();
  // But the handle shouldn't be empty.
  // That can happen if there was a stack overflow when creating the object.
  if (local_object.IsEmpty()) {
    return local_object;
  }

  v8::Persistent<v8::Object> wrapper =
      v8::Persistent<v8::Object>::New(local_object);

  // Set date time formatter as internal field of the resulting JS object.
  icu::SimpleDateFormat* date_format = InitializeDateTimeFormat(
      args[0]->ToString(), args[1]->ToObject(), args[2]->ToObject());

  if (!date_format) {
    return v8::ThrowException(v8::Exception::Error(v8::String::New(
        "Internal error. Couldn't create ICU date time formatter.")));
  } else {
    wrapper->SetAlignedPointerInInternalField(0, date_format);

    v8::TryCatch try_catch;
    wrapper->Set(v8::String::New("dateFormat"), v8::String::New("valid"));
    if (try_catch.HasCaught()) {
      return v8::ThrowException(v8::Exception::Error(
          v8::String::New("Internal error, couldn't set property.")));
    }
  }

  // Make object handle weak so we can delete iterator once GC kicks in.
  wrapper.MakeWeak(NULL, DeleteDateFormat);

  return wrapper;
}

static icu::SimpleDateFormat* InitializeDateTimeFormat(
    v8::Handle<v8::String> locale,
    v8::Handle<v8::Object> options,
    v8::Handle<v8::Object> resolved) {
  v8::HandleScope handle_scope;

  // Convert BCP47 into ICU locale format.
  UErrorCode status = U_ZERO_ERROR;
  icu::Locale icu_locale;
  char icu_result[ULOC_FULLNAME_CAPACITY];
  int icu_length = 0;
  v8::String::AsciiValue bcp47_locale(locale);
  if (bcp47_locale.length() != 0) {
    uloc_forLanguageTag(*bcp47_locale, icu_result, ULOC_FULLNAME_CAPACITY,
                        &icu_length, &status);
    if (U_FAILURE(status) || icu_length == 0) {
      return NULL;
    }
    icu_locale = icu::Locale(icu_result);
  }

  icu::SimpleDateFormat* date_format = CreateICUDateFormat(icu_locale, options);
  if (!date_format) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    date_format = CreateICUDateFormat(no_extension_locale, options);

    // Set resolved settings (pattern, numbering system, calendar).
    SetResolvedSettings(no_extension_locale, date_format, resolved);
  } else {
    SetResolvedSettings(icu_locale, date_format, resolved);
  }

  return date_format;
}

static icu::SimpleDateFormat* CreateICUDateFormat(
    const icu::Locale& icu_locale, v8::Handle<v8::Object> options) {
  // Create time zone as specified by the user. We have to re-create time zone
  // since calendar takes ownership.
  icu::TimeZone* tz = NULL;
  icu::UnicodeString timezone;
  if (Utils::ExtractStringSetting(options, "timeZone", &timezone)) {
    tz = icu::TimeZone::createTimeZone(timezone);
  } else {
    tz = icu::TimeZone::createDefault();
  }

  // Create a calendar using locale, and apply time zone to it.
  UErrorCode status = U_ZERO_ERROR;
  icu::Calendar* calendar =
      icu::Calendar::createInstance(tz, icu_locale, status);

  // Make formatter from skeleton. Calendar and numbering system are added
  // to the locale as Unicode extension (if they were specified at all).
  icu::SimpleDateFormat* date_format = NULL;
  icu::UnicodeString skeleton;
  if (Utils::ExtractStringSetting(options, "skeleton", &skeleton)) {
    icu::DateTimePatternGenerator* generator =
        icu::DateTimePatternGenerator::createInstance(icu_locale, status);
    icu::UnicodeString pattern;
    if (U_SUCCESS(status)) {
      pattern = generator->getBestPattern(skeleton, status);
      delete generator;
    }

    date_format = new icu::SimpleDateFormat(pattern, icu_locale, status);
    if (U_SUCCESS(status)) {
      date_format->adoptCalendar(calendar);
    }
  }

  if (U_FAILURE(status)) {
    delete calendar;
    delete date_format;
    date_format = NULL;
  }

  return date_format;
}

static void SetResolvedSettings(const icu::Locale& icu_locale,
                                icu::SimpleDateFormat* date_format,
                                v8::Handle<v8::Object> resolved) {
  v8::HandleScope handle_scope;

  icu::UnicodeString pattern;
  date_format->toPattern(pattern);
  resolved->Set(v8::String::New("pattern"),
                v8::String::New(reinterpret_cast<const uint16_t*>(
                    pattern.getBuffer()), pattern.length()));

  if (date_format) {
    const icu::Calendar* calendar = date_format->getCalendar();
    const char* calendar_name = calendar->getType();
    resolved->Set(v8::String::New("calendar"), v8::String::New(calendar_name));
    // Get timeZone ID.
    const icu::TimeZone& tz = calendar->getTimeZone();
    icu::UnicodeString time_zone;
    tz.getID(time_zone);
    resolved->Set(v8::String::New("timeZone"),
                  v8::String::New(reinterpret_cast<const uint16_t*>(
                      time_zone.getBuffer()), time_zone.length()));
  }

  // Ugly hack. ICU doesn't expose numbering system in any way, so we have
  // to assume that for given locale NumberingSystem constructor produces the
  // same digits as NumberFormat/Calendar would.
  UErrorCode status = U_ZERO_ERROR;
  icu::NumberingSystem* numbering_system =
      icu::NumberingSystem::createInstance(icu_locale, status);
  if (U_SUCCESS(status)) {
    const char* ns = numbering_system->getName();
    resolved->Set(v8::String::New("numberingSystem"), v8::String::New(ns));
  } else {
    resolved->Set(v8::String::New("numberingSystem"), v8::Undefined());
  }
  delete numbering_system;

  // Set the locale
  char result[ULOC_FULLNAME_CAPACITY];
  status = U_ZERO_ERROR;
  uloc_toLanguageTag(
      icu_locale.getName(), result, ULOC_FULLNAME_CAPACITY, FALSE, &status);
  if (U_SUCCESS(status)) {
    resolved->Set(v8::String::New("locale"), v8::String::New(result));
  } else {
    // This would never happen, since we got the locale from ICU.
    resolved->Set(v8::String::New("locale"), v8::String::New("und"));
  }
}

}  // namespace v8_i18n
